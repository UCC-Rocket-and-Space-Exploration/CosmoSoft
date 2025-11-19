#include <glob.h>
#include <vector>
#include <string>
#include <poll.h>
#include <sys/stat.h>
#include <iostream>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <iomanip>
#include <unistd.h>
#include <termios.h>
#include <chrono>

std::vector<std::string> enumerate_ports() {
    std::vector<std::string> result;
    const char* patterns[] = {
        "/dev/ttyUSB*",   // Linux USB-to-serial (what we likely need)
        "/dev/ttyACM*",   // Linux CDC-ACM devices (e.g. Arduino)
        //"/dev/ttyS*",     // Linux legacy serial
        "/dev/pts/*",     // PTYs created by socat (For general testing
    };


    //finds all possible interfaces matching the above pattenrs
    for (const char* p : patterns) {
        glob_t g;
        if (glob(p, 0, nullptr, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc; ++i) { //gl_pathc = number of paths matching the pattern
                result.emplace_back(g.gl_pathv[i]); //gl.pathv = ponter to the list of matched paths
                                                    //using emplace_back instead of push_back is a best practice
            }
        }
        globfree(&g); //frees storage used by glob
    }
    return result;
}

void show_stat(const std::string &path) {
    struct stat st{};
    std::chrono::seconds secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
    if (stat(path.c_str(), &st) == 0) {
        std::cout << path
                  << " mode=" << std::oct << (st.st_mode & 0777) << std::dec // mode of device? gives file flag permissions
                  << " uid=" << st.st_uid //userid of the owner
                  << " gid=" << st.st_gid //groupid of the owner
                  << " time= " << (secs.count() - st.st_mtim.tv_sec) //can be used to see when last data was written to interface (DOES NOT WORK WELL FOR SERIAL PORTS)
                  << " optimal I/O blocksize= " << st.st_blksize << "\n"; //optimal blocksize for IO

    } else {
        std::cerr << "stat failed for " << path << ": " << strerror(errno) << "\n";
    }
}

int try_open(const std::string &path) {
    // O_RDWR = read/write
    // O_NOCTTY = don’t make controlling terminal
    // O_NONBLOCK = non-blocking open
    int fd = open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK); //Check other possible flags
    if (fd < 0) return -1; // failed to open

    // Configure termios for raw mode so kernal doesnt
    termios tty;
    if (tcgetattr(fd, &tty) != 0) { close(fd); return -1; }
    cfmakeraw(&tty);             // disables canonical mode, echo, signals
    cfsetospeed(&tty, B115200);  // output baud (we are using 115200 baud rate )
    cfsetispeed(&tty, B115200);  // input baud
    tty.c_cc[VMIN] = 1;          // read blocks until >=1 byte, i.e. as long as there is data to be read
    tty.c_cc[VTIME] = 1;         // read timeout = 0.1s
    tty.c_cflag |= (CLOCAL | CREAD); // ignore modem control, enable reading
    if (tcsetattr(fd, TCSANOW, &tty) != 0) { close(fd); return -1; }

    return fd; // success
}

void read_loop(int fd) {
    struct pollfd pfd{}; //use polling file descripter
    pfd.fd = fd; //set the file descriptor to poll
    pfd.events = POLLIN | POLLERR | POLLHUP | POLLRDHUP;

    while (true) {
        int r = poll(&pfd, 1, -1); // block until data
        if (r > 0) {
            if (pfd.revents & POLLIN) {
                uint8_t buf[1024];
                ssize_t n = read(fd, buf, sizeof(buf)); //read from fd into the buffer all possible bytes
                if (n > 0) { //while there is still data in the buffer
                    std::cout << "Received " << n << " bytes: ";
                    for (ssize_t i = 0; i < n; ++i) { //print out all
                        unsigned char c = buf[i];
                        if (isprint(c)) std::cout << c;
                        else std::cout << "\\x" << std::hex << (int)c << std::dec;
                    }
                    std::cout << "\n";
                }
            }
            if (pfd.revents & POLLERR) { //general error, typically a problem with the code itself (inc. frame errors, line errors
                std::cerr << "Serial port error!\n";
            }


            if (pfd.revents & POLLHUP) { //fires if a disconnect happens without explicit closing
                std::cerr << "Serial port disconnected (HUP)!\n";
                break; // or handle reconnect
            }
            if (pfd.revents & POLLRDHUP) { //fires if the peer closes the connection
                std::cerr << "Peer closed connection (RDHUP)\n";
                break;
            }
            //if ()
        }

    }
}


int main() {
    auto ports = enumerate_ports();
    if (ports.empty()) {
        std::cout << "No candidate serial devices found.\n";
        return 1;
    }

    std::cout << "Candidate devices:\n";
    for (auto &p : ports) show_stat(p);

    int fd = -1;
    std::string chosen;

    std::cout << "\nReadable Ports:\n";
    for (auto &p : ports) {
        int tryfd = try_open(p);
        if (tryfd >= 0) { fd = tryfd; chosen = p;
            std::cout << "Port \"" << chosen << "\"\n";
        }
    }

    std::cout << "\nChoose Port:\n";
    std::cin >> chosen;
    fd = try_open(chosen);
    std::cout << "Connecting to port \"" << chosen << "\"...\n";
    if (fd < 0) {
        std::cerr << "Could not open the candidate device. Check permissions or run socat.\n";
        return 1;
    } else {
        std::cout << "Connected to port \"" << chosen << "\"\n";
    }

    read_loop(fd);
    close(fd);
    return 0;
}