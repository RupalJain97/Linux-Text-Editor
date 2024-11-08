/** Libraries */
#include <iostream>  // For input/output in C++
#include <unistd.h>  // For read() and STDIN_FILENO
#include <stdlib.h>  // For atexit()
#include <termios.h> // Terminal I/O attributes
#include <errno.h>
#include <cstring>     // For strerror
#include <sys/ioctl.h> // To get terminal dimensions
#include <stdio.h>
#include <string>
#include <vector>
#include <time.h>
#include <cstdarg> // For va_list, va_start, va_end
#include <fstream>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

/** Data */
struct erow
{
    int size;
    int rsize;
    char *chars;
    char *render;
};

struct editorConfig
{
    int cx, cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    char *filename;
    int dirty;
    struct termios orig_termios;
    char statusmsg[80];
    time_t statusmsg_time;
};
struct editorConfig E;

enum editorKey
{
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** prototypes ***/
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
std::string editorPrompt(const std::string &prompt, void (*callback)(const std::string &, int));

/** Terminal */
void die(const char *s)
{
    /* Clear the screen on exit */
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode()
{
    // tcgetattr(STDIN_FILENO, &orig_termios); // Get current terminal attributes
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");

    atexit(disableRawMode); // Ensure original settings are restored on exit

    struct termios raw = E.orig_termios; // Make a copy of the terminal settings

    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);          // Disable echo, canonical mode, Ctrl-C/Z signals
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // Disable input flags
    raw.c_oflag &= ~(OPOST);                                  // Disable output processing
    raw.c_cflag |= (CS8);                                     // Set character size to 8 bits

    // raw.c_cc[VMIN] = 0;  // Set minimum number of bytes to read
    // raw.c_cc[VTIME] = 1; // Set timeout for reading

    // Apply new settings
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

int editorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }
    if (c == '\x1b')
    {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                    case '1':
                        return HOME_KEY;
                    case '3':
                        return DEL_KEY;
                    case '4':
                        return END_KEY;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '7':
                        return HOME_KEY;
                    case '8':
                        return END_KEY;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O')
        {
            switch (seq[1])
            {
            case 'H':
                return HOME_KEY;
            case 'F':
                return END_KEY;
            }
        }

        return '\x1b';
    }
    else
    {
        return c;
    }
}

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    // Read the response into the buffer
    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break; // 'R' signifies the end of the response
        i++;
    }
    buf[i] = '\0'; // Null-terminate the buffer

    // Print the buffer contents (response from terminal)
    std::cout << "\r\nResponse: '" << &buf[1] << "'\r\n";

    // Check response format and parse
    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    editorReadKey();
    return -1;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        // Move the cursor to the bottom-right corner as a fallback
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;

        return getCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** row operations ***/
int editorRowCxToRx(erow *row, int cx)
{
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++)
    {
        if (row->chars[j] == '\t')
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        rx++;
    }
    return rx;
}

int editorRowRxToCx(erow *row, int rx)
{
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++)
    {
        if (row->chars[cx] == '\t')
            cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
        cur_rx++;
        if (cur_rx > rx)
            return cx;
    }
    return cx;
}

void editorUpdateRow(erow *row)
{
    free(row->render);

    // For each tab, we may need up to 8 spaces, so allocate accordingly
    int tabs = 0;
    for (int j = 0; j < row->size; j++)
    {
        if (row->chars[j] == '\t')
            tabs++;
    }

    row->render = (char *)malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1);
    int idx = 0;

    for (int j = 0; j < row->size; j++)
    {
        if (row->chars[j] == '\t')
        {
            row->render[idx++] = ' ';
            while (idx % (KILO_TAB_STOP) != 0)
                row->render[idx++] = ' ';
        }
        else
        {
            row->render[idx++] = row->chars[j];
        }
    }

    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorRowInsertChar(erow *row, int at, char c)
{
    // Ensure 'at' is within bounds
    if (at < 0 || at > row->size)
        at = row->size;

    row->chars = (char *)realloc(row->chars, row->size + 2); // Adjust size for new char + null byte
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at);
    row->chars[at] = c;
    row->size++;

    // Update render vector and rsize accordingly
    editorUpdateRow(row);
    E.dirty++;
}

// Append a new row to the editor's row array
void editorInsertRow(int at, const char *s, size_t len)
{
    if (at < 0 || at > E.numrows)
        return;

    E.row = (erow *)realloc(E.row, sizeof(erow) * (E.numrows + 1));
    std::memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

    E.row[at].size = len;
    E.row[at].chars = (char *)malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = nullptr;
    editorUpdateRow(&E.row[at]);
    E.numrows++;
    E.dirty++;
}

void editorRowDelChar(erow *row, int at)
{
    if (at < 0 || at >= row->size)
        return;

    std::memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

void editorFreeRow(erow *row)
{
    free(row->render);
    free(row->chars);
}

void editorDelRow(int at)
{
    if (at < 0 || at >= E.numrows)
        return;

    editorFreeRow(&E.row[at]);
    std::memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    E.numrows--;
    E.dirty++;
}

void editorRowAppendString(erow *row, const char *s, size_t len)
{
    row->chars = (char *)realloc(row->chars, row->size + len + 1);
    std::memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

/*** editor operations ***/
void editorInsertChar(int c)
{
    if (E.cy == E.numrows)
    {
        editorInsertRow(E.numrows, "", 0); // Append a new empty row if needed
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editorDelChar()
{
    if (E.cy == E.numrows)
        return;

    if (E.cx == 0 && E.cy == 0)
        return;
    erow *row = &E.row[E.cy];
    if (E.cx > 0)
    {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    }
    else
    {
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}

void editorInsertNewline()
{
    if (E.cx == 0)
    {
        editorInsertRow(E.cy, "", 0);
    }
    else
    {
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);

        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }

    E.cy++;
    E.cx = 0;
    E.dirty++;
}

/*** file i/o ***/
std::string editorRowsToString(int &buflen)
{
    buflen = 0;
    for (int j = 0; j < E.numrows; j++)
    {
        buflen += E.row[j].size + 1; // +1 for newline character
    }

    std::string buffer;
    buffer.reserve(buflen); // Reserve exact space to avoid reallocations

    for (int j = 0; j < E.numrows; j++)
    {
        buffer.append(E.row[j].chars);
        buffer.append("\n"); // Append newline
    }
    return buffer;
}

void editorSave()
{
    if (E.filename == nullptr)
    {
        E.filename = strdup(editorPrompt("Save as: %s (ESC to cancel)", NULL).c_str());
        if (E.filename == nullptr || E.filename[0] == '\0')
        { // Check for cancel
            editorSetStatusMessage("Save aborted");
            return;
        }
    }

    int len;
    std::string buffer = editorRowsToString(len);

    std::ofstream file(E.filename, std::ios::out | std::ios::trunc);
    if (file)
    {
        file.write(buffer.c_str(), len);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk... File saved successfully", len);
    }
    else
    {
        editorSetStatusMessage("Can't save! I/O error");
    }
}

/*** find ***/
void editorFindCallback(const std::string &query, int key)
{
    static int last_match = -1;
    static int direction = 1;

    if (key == '\r' || key == '\x1b')
    {
        last_match = -1;
        direction = 1;
        return;
    }
    else if (key == ARROW_RIGHT || key == ARROW_DOWN)
    {
        direction = 1;
    }
    else if (key == ARROW_LEFT || key == ARROW_UP)
    {
        direction = -1;
    }
    else
    {
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1)
        direction = 1;
    int current = last_match;
    for (int i = 0; i < E.numrows; i++)
    {
        current += direction;
        if (current == -1)
            current = E.numrows - 1;
        else if (current == E.numrows)
            current = 0;

        erow *row = &E.row[current];
        const char *match = strstr(row->render, query.c_str());
        if (match)
        {
            last_match = current;
            E.cy = current;
            E.cx = editorRowRxToCx(row, match - row->render);
            E.rowoff = E.numrows;
            break;
        }
    }
}

void editorFind()
{
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;

    std::string query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);
    if (query.empty())
        return;
    else
    {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloff = saved_coloff;
        E.rowoff = saved_rowoff;
    }
}

// Open a file and load its contents into the editor
void editorOpen(const char *filename)
{
    E.filename = strdup(filename);
    FILE *fp = fopen(filename, "r");
    if (!fp)
        die("fopen");

    char *line = nullptr;
    size_t linecap = 0;
    ssize_t linelen = 13;

    while ((linelen = getline(&line, &linecap, fp)) != -1)
    {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;
        editorInsertRow(E.numrows, line, linelen);
    }

    free(line);
    fclose(fp);
    E.dirty = 0;
}

/*** Input ***/
void editorMoveCursor(int key)
{
    // Check if the cursor is within a valid line; otherwise, set `row` to `nullptr`
    erow *row = (E.cy >= E.numrows) ? nullptr : &E.row[E.cy];

    switch (key)
    {
    case ARROW_LEFT:
        if (E.cx != 0)
        {
            E.cx--;
        }
        else if (E.cy > 0)
        {
            E.cy--;
            E.cx = E.row[E.cy].size;
        }
        break;
    case ARROW_RIGHT:
        if (row && E.cx < row->size)
        {
            E.cx++;
        }
        else if (row && E.cx == row->size)
        {
            E.cy++;
            E.cx = 0;
        }
        break;
    case ARROW_UP:
        if (E.cy != 0)
        {
            E.cy--;
        }
        break;
    case ARROW_DOWN:
        if (E.cy < E.numrows)
        {
            E.cy++;
        }
        break;
    }

    row = (E.cy >= E.numrows) ? nullptr : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen)
    {
        E.cx = rowlen;
    }
}

void editorProcessKeypress()
{
    static int quit_times = KILO_QUIT_TIMES;
    int c = editorReadKey();

    switch (c)
    {
    case '\r':
        editorInsertNewline();
        break;

    case CTRL_KEY('q'):
        if (E.dirty && quit_times > 0)
        {
            editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                                   "Press Ctrl-Q %d more times to quit.",
                                   quit_times);
            quit_times--;
            return;
        }
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

    case CTRL_KEY('s'):
        editorSave();
        break;

    case HOME_KEY:
        E.cx = 0;
        break;

    case END_KEY:
        if (E.cy < E.numrows)
            E.cx = E.row[E.cy].size;
        break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
        if (c == DEL_KEY)
            editorMoveCursor(ARROW_RIGHT);
        editorDelChar();
        break;

    case PAGE_UP:
    case PAGE_DOWN:
    {
        if (c == PAGE_UP)
        {
            E.cy = E.rowoff;
        }
        else if (c == PAGE_DOWN)
        {
            E.cy = E.rowoff + E.screenrows - 1;
            if (E.cy > E.numrows)
                E.cy = E.numrows;
        }
        int times = E.screenrows;
        while (times--)
            editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    }
    break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;

    case CTRL_KEY('f'):
        editorFind();
        break;

    case CTRL_KEY('l'):
    case '\x1b':
        break;

    default:
        editorInsertChar(c);
        break;
    }
    quit_times = KILO_QUIT_TIMES;
}

std::string editorPrompt(const std::string &prompt, void (*callback)(const std::string &, int) = nullptr)
{
    size_t buflen = 0;
    std::string buf;
    buf.reserve(128);

    while (true)
    {
        editorSetStatusMessage(prompt.c_str(), buf.c_str());
        editorRefreshScreen();

        int c = editorReadKey();

        if (c == '\x1b')
        { // ESC to cancel
            editorSetStatusMessage("");
            if (callback)
                callback(buf, c);
            return "";
        }
        else if (c == '\r')
        { // Enter to confirm
            if (!buf.empty())
            {
                editorSetStatusMessage("");
                if (callback)
                    callback(buf, c);
                return buf;
            }
        }
        else if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE)
        { // Backspace
            if (!buf.empty())
                buf.pop_back();
        }
        else if (!iscntrl(c) && c < 128)
        { // Add printable character
            buf += c;
            buflen++;
        }

        if (callback)
            callback(buf, c);
    }
}

/*** Output ***/
void editorScroll()
{
    E.rx = 0;

    if (E.cy < E.numrows)
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);

    if (E.cy < E.rowoff)
        E.rowoff = E.cy;
    if (E.cy >= E.rowoff + E.screenrows)
        E.rowoff = E.cy - E.screenrows + 1;

    if (E.rx < E.coloff)
        E.coloff = E.rx;
    if (E.rx >= E.coloff + E.screencols)
        E.coloff = E.rx - E.screencols + 1;
}

void editorDrawRows(std::string &ab)
{
    for (int y = 0; y < E.screenrows; y++)
    {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows)
        {
            if (E.numrows == 0 && y == E.screenrows / 3)
            {
                // Display the welcome message a third of the way down
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
                if (welcomelen > E.screencols)
                    welcomelen = E.screencols;

                int padding = (E.screencols - welcomelen) / 2;
                if (padding)
                {
                    ab.append("~");
                    padding--;
                }
                while (padding--)
                    ab.append(" ");
                ab.append(welcome, welcomelen);
            }
            else
            {
                ab.append("~");
            }
        }
        else
        {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0)
                len = 0;
            if (len > E.screencols)
                len = E.screencols;
            ab.append(&E.row[filerow].render[E.coloff], len);
        }
        ab.append("\x1b[K"); // clear lines as we draw
                             // if (y < E.screenrows - 1)
                             // {
        // write(STDOUT_FILENO, "\r\n", 2);
        ab.append("\r\n");
        // }
    }
}

void editorDrawStatusBar(std::string &ab)
{
    ab.append("\x1b[7m"); // Invert colors
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines",
                       E.filename ? E.filename : "[No Name]", E.numrows, E.dirty ? "(modified)" : "");

    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
                        E.cy + 1, E.numrows);
    if (len > E.screencols)
        len = E.screencols;
    ab.append(status);
    while (len < E.screencols)
    {
        if (E.screencols - len == rlen)
        {
            ab.append(rstatus);
            break;
        }
        else
        {
            ab.append(" ");
            len++;
        }
    }
    ab.append("\x1b[m"); // Reset to normal formatting
    ab.append("\r\n");
}

void editorDrawMessageBar(std::string &ab)
{
    ab.append("\x1b[K"); // Clear the line
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols)
        msglen = E.screencols;
    if (msglen && time(nullptr) - E.statusmsg_time < 5)
        ab.append(E.statusmsg, msglen);
}

void editorRefreshScreen()
{
    editorScroll();
    /*
    write() and STDOUT_FILENO come from <unistd.h>.

    The 4 in our write() call means we are writing 4 bytes out to the terminal. The first byte is \x1b, which is the escape character, or 27 in decimal. (Try and remember \x1b, we will be using it a lot.) The other three bytes are [2J.
    */
    // write(STDOUT_FILENO, "\x1b[2J", 4);

    /*
    reposition the cursor which is at the top-left corner so that we’re ready to draw the editor interface from top to bottom.

    if you have an 80×24 size terminal and you want the cursor in the center of the screen, you could use the command <esc>[12;40H. (Multiple arguments are separated by a ; character.) The default arguments for H both happen to be 1.
    */
    // write(STDOUT_FILENO, "\x1b[H", 3);

    std::string ab;

    ab.append("\x1b[?25l"); // Hide the cursor

    // Append escape sequences to clear the screen and move cursor to top-left
    // ab.append("\x1b[2J");  // Clear the screen
    ab.append("\x1b[H"); // Move cursor to the top-left corner

    editorDrawRows(ab);
    editorDrawStatusBar(ab);
    editorDrawMessageBar(ab);

    // write(STDOUT_FILENO, "\x1b[H", 3);
    // Move the cursor back to the top-left corner
    ab.append("\x1b[H");

    char buf[32];
    int welcomelen = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
    ab.append(buf);

    ab.append("\x1b[?25h"); // Hide the cursor

    // Write the buffer contents to standard output
    write(STDOUT_FILENO, ab.c_str(), ab.size());
}

void editorSetStatusMessage(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(nullptr);
}

/*** Init ***/
//  initialize all the fields in the E struct.
void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = nullptr;
    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");

    E.screenrows -= 2; // Reserve one row for the status bar
    E.filename = nullptr;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.dirty = 0;
}

int main(int argc, char *argv[])
{
    std::cout << "Welcome to the text Editor\n";
    // std::cout << "This is the raw mode.\n";
    // std::cout << "Raw mode is a terminal setting that allows the program to read input directly from the user without buffering or processing (like echoing characters or interpreting special keys). This lets the editor respond immediately to each keypress for an interactive editing experience.\n";
    // std::cout << "Enter 'q' to exit.\n";

    enableRawMode();
    initEditor();

    if (argc >= 2)
    {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-F = find | Ctrl-S = save | Ctrl-Q = quit");
    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
