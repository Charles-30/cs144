#include "socket.hh"
#include "util.hh"

#include <cstdlib>
#include <iostream>

using namespace std;

void get_URL(const string &host, const string &path) {
    // Resolve the host and connect to the HTTP service, then issue a
    // simple HTTP/1.1 GET request including Connection: close, and
    // print everything the server sends until EOF.

    // Create address for host on service "http" (port 80)
    Address server_address{host, "http"};

    // Create a TCP socket and connect
    TCPSocket socket;
    socket.connect(server_address);

    // Build an HTTP/1.1 GET request. Each line must end with \r\n.
    // Include Host header and Connection: close so server will close
    // the connection after the response is sent.
    string request;
    request += "GET " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Connection: close\r\n";
    request += "\r\n";  // blank line to end headers

    // Send the request
    socket.write(request);

    // Read until EOF and write to stdout
    while (!socket.eof()) {
        string chunk = socket.read();
        cout << chunk;
    }

    // Ensure output is flushed
    cout.flush();
}

int main(int argc, char *argv[]) {
    try {
        if (argc <= 0) {
            abort();  // For sticklers: don't try to access argv[0] if argc <= 0.
        }

        // The program takes two command-line arguments: the hostname and "path" part of the URL.
        // Print the usage message unless there are these two arguments (plus the program name
        // itself, so arg count = 3 in total).
        if (argc != 3) {
            cerr << "Usage: " << argv[0] << " HOST PATH\n";
            cerr << "\tExample: " << argv[0] << " stanford.edu /class/cs144\n";
            return EXIT_FAILURE;
        }

        // Get the command-line arguments.
        const string host = argv[1];
        const string path = argv[2];

        // Call the student-written function.
        get_URL(host, path);
    } catch (const exception &e) {
        cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
