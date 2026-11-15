#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include "keyboard.h"
#include "events.h"

static pthread_t keyboard_thread;
static struct termios saved_terminal;
static atomic_bool keyboard_running = ATOMIC_VAR_INIT(false);
static bool keyboard_started = false;

typedef enum {
    KEY_NORMAL,
    KEY_ESCAPE,
    KEY_CSI,
    KEY_SS3
} KeyState;

static void handle_key(char key)
{
    switch (tolower((unsigned char)key)) {
    case 'a':
        events_push(EV_LEFT, SRC_KEYBOARD);
        break;
    case 'd':
        events_push(EV_RIGHT, SRC_KEYBOARD);
        break;
    case 'w':
        events_push(EV_JUMP, SRC_KEYBOARD);
        break;
    case 's':
        events_push(EV_CROUCH, SRC_KEYBOARD);
        break;
    case 'i':
        events_push(EV_UP, SRC_KEYBOARD);
        break;
    case 'k':
        events_push(EV_DOWN, SRC_KEYBOARD);
        break;
    case '\n':
    case '\r':
        events_push(EV_SELECT, SRC_KEYBOARD);
        break;
    case 'q':
        events_push(EV_BACK, SRC_KEYBOARD);
        break;
    }
}

static void handle_arrow(char key)
{
    switch (key) {
    case 'A':
        events_push(EV_UP, SRC_KEYBOARD);
        break;
    case 'B':
        events_push(EV_DOWN, SRC_KEYBOARD);
        break;
    case 'C':
        events_push(EV_RIGHT, SRC_KEYBOARD);
        break;
    case 'D':
        events_push(EV_LEFT, SRC_KEYBOARD);
        break;
    }
}

static void handle_input_byte(char key, KeyState *state)
{
    unsigned char ch = (unsigned char)key;

    if (*state == KEY_NORMAL) {
        if (ch == 0x1b) {
            *state = KEY_ESCAPE;
            return;
        }

        handle_key(key);
        return;
    }

    if (*state == KEY_ESCAPE) {
        if (ch == '[') {
            *state = KEY_CSI;
        } else if (ch == 'O') {
            *state = KEY_SS3;
        } else {
            *state = KEY_NORMAL;
            handle_key(key);
        }
        return;
    }

    if (*state == KEY_CSI) {
        if (ch >= 0x40 && ch <= 0x7e) {
            *state = KEY_NORMAL;
            handle_arrow(key);
        }
        return;
    }

    if (*state == KEY_SS3) {
        *state = KEY_NORMAL;
        handle_arrow(key);
    }
}

static void *keyboard_worker(void *arg)
{
    (void)arg;

    KeyState state = KEY_NORMAL;

    while (atomic_load(&keyboard_running)) {
        struct pollfd input = {
            .fd = STDIN_FILENO,
            .events = POLLIN
        };

        int result = poll(&input, 1, 100);

        if (result < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (result == 0)
            continue;

        if (input.revents & (POLLERR | POLLHUP | POLLNVAL))
            break;

        if (input.revents & POLLIN) {
            char key;
            if (read(STDIN_FILENO, &key, 1) == 1)
                handle_input_byte(key, &state);
        }
    }

    return NULL;
}

bool keyboard_start(void)
{
    if (keyboard_started)
        return true;

    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "Keyboard requires a terminal\n");
        return false;
    }

    if (tcgetattr(STDIN_FILENO, &saved_terminal) != 0) {
        perror("tcgetattr");
        return false;
    }

    struct termios terminal = saved_terminal;
    terminal.c_lflag &= ~(ICANON | ECHO);
    terminal.c_cc[VMIN] = 0;
    terminal.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &terminal) != 0) {
        perror("tcsetattr");
        return false;
    }

    atomic_store(&keyboard_running, true);

    int result = pthread_create(
        &keyboard_thread, NULL, keyboard_worker, NULL
    );

    if (result != 0) {
        atomic_store(&keyboard_running, false);
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_terminal);
        fprintf(stderr, "Failed to start keyboard thread: %d\n", result);
        return false;
    }

    keyboard_started = true;
    return true;
}

void keyboard_stop(void)
{
    if (!keyboard_started)
        return;

    atomic_store(&keyboard_running, false);
    pthread_join(keyboard_thread, NULL);

    if (tcsetattr(STDIN_FILENO, TCSANOW, &saved_terminal) != 0)
        perror("tcsetattr");

    keyboard_started = false;
}
