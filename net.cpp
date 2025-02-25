#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <sstream>
#include <map>  // Include this to use std::map

class HttpServer {
private:
    int server_fd, client_socket;
    struct sockaddr_in address;
    int port;
    int backlog;

    struct HttpRequest {
        std::string method;
        std::string path;
        std::string httpVersion;
        std::map<std::string, std::string> headers;  // std::map is used here
        std::string body;

        HttpRequest(const std::string& request) {
            std::istringstream stream(request);
            std::string line;
            getline(stream, line);
            std::istringstream requestLine(line);
            requestLine >> method >> path >> httpVersion;

            while (getline(stream, line) && line != "\r") {
                auto colonPos = line.find(':');
                if (colonPos != std::string::npos) {
                    std::string headerName = line.substr(0, colonPos);
                    std::string headerValue = line.substr(colonPos + 2, line.size() - colonPos - 3); // skip colon and space, trim \r
                    headers[headerName] = headerValue;
                }
            }
            // Assuming body follows immediately after headers and an empty line
            while (getline(stream, line)) {
                body += line + "\n";
            }
        }
    };

    void log(const std::string& level, const std::string& className, const std::string& method, const std::string& why, const std::string& data) {
        std::cout << "[" << level << "][" << className << "][" << method << "] <" << why << "> " << data << std::endl;
    }

public:
    HttpServer(int port, int backlog = 10) : port(port), backlog(backlog) {
        this->server_fd = 0;
        this->client_socket = 0;
        this->address = {0};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
    }

    ~HttpServer() {
        close(this->server_fd);
    }

    bool initialize() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1) {
            log("ERROR", "HttpServer", "initialize", "Socket creation", "failed");
            return false;
        }

        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
            log("ERROR", "HttpServer", "initialize", "Setting socket options", "failed");
            return false;
        }

        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
            log("ERROR", "HttpServer", "initialize", "Binding socket", "failed");
            return false;
        }

        if (listen(server_fd, backlog) == -1) {
            log("ERROR", "HttpServer", "initialize", "Listening on socket", "failed");
            return false;
        }

        log("INFO", "HttpServer", "initialize", "Server initialization", "successful");
        return true;
    }

    void run() {
        log("INFO", "HttpServer", "run", "Server start", "Waiting for connections...");
        char buffer[3000] = {0};
    
        while (true) {
            socklen_t addrlen = sizeof(address);
            client_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
            if (client_socket == -1) {
                log("ERROR", "HttpServer", "run", "Accepting connection", "failed");
                continue;
            }
            memset(buffer, 0, sizeof(buffer));
            read(client_socket, buffer, sizeof(buffer) - 1);
            HttpRequest request(buffer);
            log("INFO", "HttpServer", "run", "Request received", request.path);
    
            std::string response;
            if (request.path == "/") {
                response = "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: 12\n\nHello world!";
            } 
            else if (request.path == "/favicon.ico") {  // Handle favicon requests
                response = "HTTP/1.1 404 Not Found\nContent-Type: text/plain\nContent-Length: 0\n\n";
            } 
            else {
                response = "HTTP/1.1 404 Not Found\nContent-Type: text/plain\nContent-Length: 3\n\n404";
            }
    
            write(client_socket, response.c_str(), response.size());
            log("INFO", "HttpServer", "run", "Response sent", response);
            close(client_socket);
        }
    }
};

int main() {
    HttpServer server(8080);
    if (!server.initialize()) {
        return EXIT_FAILURE;
    }
    server.run();
    return 0;
}
