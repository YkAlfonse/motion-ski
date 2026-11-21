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
static atomic_bool name_mode = ATOMIC_VAR_INIT(false);
static pthread_mutex_t name_lock = PTHREAD_MUTEX_INITIALIZER;
static char name_keys[64];
static int name_head;
static int name_count;


typedef enum {
    KEY_NORMAL,
    KEY_ESCAPE,
    KEY_CSI,
    KEY_SS3
} KeyState;

static void queue_name_key(char key)
{
    pthread_mutex_lock(&name_lock);
    if (name_count < 64) {
        name_keys[(name_head + name_count) % 64] = key;
        name_count++;
    }
    pthread_mutex_unlock(&name_lock);
}

void keyboard_set_name_mode(bool enabled)
{
    pthread_mutex_lock(&name_lock);
    name_head = 0;
    name_count = 0;
    atomic_store(&name_mode, enabled);
    pthread_mutex_unlock(&name_lock);
}

bool keyboard_pop_name_key(char *key)
{
    if (!key)
        return false;

    pthread_mutex_lock(&name_lock);

    if (name_count == 0) {
        pthread_mutex_unlock(&name_lock);
        return false;
    }

    *key = name_keys[name_head];
    name_head = (name_head + 1) % 64;
    name_count--;

    pthread_mutex_unlock(&name_lock);
    return true;
}

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

        if (atomic_load(&name_mode)) {
            if (ch == '\n' || ch == '\r')
                events_push(EV_SELECT, SRC_KEYBOARD);
            else if (ch == 8 || ch == 127)
                queue_name_key('\b');
            else if ((ch >= 'A' && ch <= 'Z') ||
                     (ch >= 'a' && ch <= 'z') ||
                     (ch >= '0' && ch <= '9') ||
                     ch == '_' || ch == '-')
                queue_name_key((char)toupper(ch));
        } else {
            handle_key(key);
        }
        return;
    }

    if (*state == KEY_ESCAPE) {
        if (ch == '[') {
            *state = KEY_CSI;
        } else if (ch == 'O') {
            *state = KEY_SS3;
        } else {
            *state = KEY_NORMAL;
            if (atomic_load(&name_mode))
                events_push(EV_BACK, SRC_KEYBOARD);
            else
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

        if (result == 0) {
            if (state == KEY_ESCAPE && atomic_load(&name_mode)) {
                events_push(EV_BACK, SRC_KEYBOARD);
                state = KEY_NORMAL;
            }
            continue;
        }

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
