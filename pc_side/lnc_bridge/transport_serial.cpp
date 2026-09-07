#include "transport_serial.h"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>

static int g_fd = -1;

bool Serial_Open(const char *device, int baudRate)
{
    g_fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (g_fd < 0) {
        return false;
    }

    struct termios tty{};
    if (tcgetattr(g_fd, &tty) != 0) {
        close(g_fd);
        g_fd = -1;
        return false;
    }

    speed_t speed = (baudRate == 9600) ? B9600 : B115200; /* extend as needed */
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= (CLOCAL | CREAD);

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);          /* raw mode, no line editing */
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
    tty.c_oflag &= ~OPOST;                                    /* raw output, no translation */

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(g_fd, TCSANOW, &tty) != 0) {
        close(g_fd);
        g_fd = -1;
        return false;
    }

    return true;
}

bool Serial_Send(const uint8_t *data, size_t len)
{
    if (g_fd < 0) return false;
    ssize_t written = write(g_fd, data, len);
    return written == static_cast<ssize_t>(len);
}

size_t Serial_Recv(uint8_t *outBuf, size_t maxLen)
{
    if (g_fd < 0) return 0;
    ssize_t n = read(g_fd, outBuf, maxLen);
    if (n < 0) return 0;   /* EAGAIN (nothing available yet) or a real error — either way, nothing to report */
    return static_cast<size_t>(n);
}

void Serial_Close(void)
{
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
}
