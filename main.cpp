#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <algorithm>
#include <list>
#include <unordered_map>

#define STATUS_SUCCESS 200
#define STATUS_NOT_FOUND 404
#define STATUS_METHOD_NOT_ALLOWED 405

void log(const std::string& level, const std::string& className, const std::string& method, const std::string& why, const std::string& data) {
    std::cout << "[" << level << "][" << className << "][" << method << "] <" << why << "> " << data << std::endl;
}

std::string getContentType(const std::string& filename) {
    std::unordered_map<std::string, std::string> extensionToContentType = {
        {".html", "text/html"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".png", "image/png"},
        {".css", "text/css"},
        {".js", "application/javascript"}
    };

    size_t dotPosition = filename.find_last_of('.');
    if (dotPosition != std::string::npos) {
        std::string extension = filename.substr(dotPosition);
        if (extensionToContentType.count(extension)) {
            log("INFO", "getContentType", "Extension match", "Content-Type found for", extension);
            return extensionToContentType[extension];
        } else {
            log("WARN", "getContentType", "Extension mismatch", "No content type for", extension);
        }
    }
    return "application/octet-stream"; // Default content type if no match found
}

struct Request {
    std::string method;
    std::string path;
    std::string httpVersion;
    std::map<std::string, std::string> headers;
    std::string body;

    Request(const std::string& requestText) {
        std::istringstream stream(requestText);
        std::string line;
        std::getline(stream, line);
        std::istringstream requestLine(line);
        requestLine >> method >> path >> httpVersion;

        while (std::getline(stream, line) && line != "\r") {
            auto colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string headerName = line.substr(0, colonPos);
                std::string headerValue = line.substr(colonPos + 2, line.size() - colonPos - 3); // Skip colon and space, trim \r
                headers[headerName] = headerValue;
            }
        }

        // Read body if present
        std::string bodyContent;
        while (std::getline(stream, bodyContent)) {
            body += bodyContent + "\n";
        }
        log("INFO", "Request", "Constructor", "Parsed request", "Method: " + method + ", Path: " + path);
    }
};

struct Response {
    int code;
    std::string body;
    std::string contentType;

    std::string buildResponse() const {
        std::ostringstream response;
        response << "HTTP/1.1 " << code << " "
                 << (code == STATUS_SUCCESS ? "OK" : (code == STATUS_NOT_FOUND ? "Not Found" : "Method Not Allowed")) << "\n"
                 << "Content-Type: " << contentType << "\n"
                 << "Content-Length: " << body.length() << "\n\n"
                 << body;
        return response.str();
    }
};

struct RouteEntry {
    std::list<std::string> allowedMethods;
    std::string content;
    bool isFile;
};

class RequestHandler {
public:
    RequestHandler() {
        RouteEntry index = {{"GET"}, "./templates/index.html", true};
        routeLookUp["/"] = index;

        RouteEntry test1 = {{"GET"}, "./templates/test.html", true};
        routeLookUp["/test/get"] = test1;
        RouteEntry test2 = {{"POST"}, "./templates/test.html", true};
        routeLookUp["/test/post"] = test2;
        RouteEntry test3 = {{"PUT"}, "./templates/test.html", true};
        routeLookUp["/test/put"] = test3;   
        RouteEntry test4 = {{"GET", "POST"}, "./templates/test.html", true};
        routeLookUp["/test/post-get"] = test4;     

        RouteEntry favicon = {{"GET"}, "./static/img/favicon.jpg", true};
        routeLookUp["/favicon.ico"] = favicon;
    }

    Response handleRequest(const Request& request) {
        auto route = routeLookUp.find(request.path);
        if (route == routeLookUp.end()) {
            log("ERROR", "handleRequest", "Route not found", "No route for", request.path);
            return {STATUS_NOT_FOUND, "<html><body>404 Route Not Found: " + request.path + "</body></html>", "text/html"};
        }

        const auto& allowedMethods = route->second.allowedMethods;
        if (std::find(allowedMethods.begin(), allowedMethods.end(), request.method) == allowedMethods.end()) {
            std::string allowed;
            for (const auto& method : allowedMethods) {
                allowed += method + " ";
            }
            log("ERROR", "handleRequest", "Method not allowed", "Method: " + request.method + " not allowed for", request.path);
            return {STATUS_METHOD_NOT_ALLOWED, "<html><body>405 Method Not Allowed: " + request.method + " not allowed for " + request.path + ". Allowed methods: " + allowed + "</body></html>", "text/html"};
        }

        if (route->second.isFile) {
            std::ifstream file(route->second.content);
            if (!file) {
                log("ERROR", "handleRequest", "File not found", "Failed to open", route->second.content);
                return {STATUS_NOT_FOUND, "<html><body>404 Resource Not Found: " + request.path + "</body></html>", "text/html"};
            }
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::string contentType = getContentType(route->second.content);
            log("INFO", "handleRequest", "File served", "Serving content from", route->second.content);
            return {STATUS_SUCCESS, content, contentType};
        } else {
            return {STATUS_SUCCESS, route->second.content, "text/html"};
        }
    }

private:
    std::map<std::string, RouteEntry> routeLookUp;
};

class HttpServer {
public:
    HttpServer(int port, int backlog = 10) : port(port), backlog(backlog), server_fd(0), client_socket(0) {
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
    }

    ~HttpServer() {
        close(server_fd);
        log("INFO", "HttpServer", "Destructor", "Server shutdown", "Port: " + std::to_string(port));
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
            Request request(buffer);
            log("INFO", "HttpServer", "run", "Request received", "Path: " + request.path);

            Response response = requestHandler.handleRequest(request);
            std::string httpResponse = response.buildResponse();

            write(client_socket, httpResponse.c_str(), httpResponse.size());
            log("INFO", "HttpServer", "run", "Response sent", "Content Length: " + std::to_string(httpResponse.size()));
            close(client_socket);
        }
    }

private:
    RequestHandler requestHandler;
    int server_fd;
    int client_socket;
    struct sockaddr_in address;
    int port;
    int backlog;
};

int main() {
    HttpServer server(8080);
    if (!server.initialize()) {
        return EXIT_FAILURE;
    }
    server.run();
    return EXIT_SUCCESS;
}
