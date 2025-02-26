import socket
import os
import logging
from colorlog import ColoredFormatter

# HTTP Status Codes and Messages
STATUS_SUCCESS = 200
STATUS_NOT_FOUND = 404
STATUS_METHOD_NOT_ALLOWED = 405

# Set up colored logging
logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger('HTTPServer')
handler = logging.StreamHandler()
formatter = ColoredFormatter(
    "%(log_color)s%(asctime)s - %(levelname)s - %(message)s",
    datefmt=None,
    reset=True,
    log_colors={
        'DEBUG': 'blue',  # Detailed steps in blue
        'INFO': 'green',  # General info in green
        'WARNING': 'yellow',
        'ERROR': 'red',  # Errors in red
        'CRITICAL': 'red,bg_white',
    },
    style='%'
)
handler.setFormatter(formatter)
logger.addHandler(handler)

def log_decorator(phase):
    def decorator(func):
        def wrapper(*args, **kwargs):
            logger.info(f"[{phase} START]")
            result = func(*args, **kwargs)
            logger.info(f"[{phase} END]")
            return result
        return wrapper
    return decorator

def get_content_type(filename):
    extension_to_content_type = {
        ".html": "text/html",
        ".jpg": "image/jpeg",
        ".jpeg": "image/jpeg",
        ".png": "image/png",
        ".css": "text/css",
        ".js": "application/javascript"
    }
    extension = os.path.splitext(filename)[1]
    return extension_to_content_type.get(extension, "application/octet-stream")

class Request:
    def __init__(self, raw_request):
        logger.debug("[REQUEST INCOMING]")
        lines = raw_request.split("\r\n")
        request_line = lines[0].split(" ")
        self.method = request_line[0] if len(request_line) > 0 else ""
        self.path = request_line[1] if len(request_line) > 1 else "/"
        self.http_version = request_line[2] if len(request_line) > 2 else "HTTP/1.1"
        self.headers = {}
        body_index = None
        for i, line in enumerate(lines[1:], 1):
            if line == "":
                body_index = i + 1
                break
            if ": " in line:
                key, value = line.split(": ", 1)
                self.headers[key] = value
        self.body = "\n".join(lines[body_index:]) if body_index else ""
        logger.debug("[REQUEST PARSED] Method: {}, Path: {}, HTTP Version: {}".format(self.method, self.path, self.http_version))

class Response:
    def __init__(self, code: int, body: bytes or str, content_type: str):
        self.code = code
        self.body = body
        self.content_type = content_type

    def build_response(self) -> bytes:
        status_text = {200: "OK", 404: "Not Found", 405: "Method Not Allowed"}.get(self.code, "Unknown")
        response_headers = f"HTTP/1.1 {self.code} {status_text}\r\n"
        response_headers += f"Content-Type: {self.content_type}\r\n"
        response_headers += f"Content-Length: {len(self.body) if isinstance(self.body, bytes) else len(self.body.encode('utf-8'))}\r\n\r\n"
        response_headers_encoded = response_headers.encode('utf-8')
        if isinstance(self.body, bytes):
            return response_headers_encoded + self.body
        else:
            return response_headers_encoded + self.body.encode('utf-8')

class RequestHandler:
    def __init__(self):
        self.route_lookup = {
            "/": {"allowed_methods": ["GET"], "content": "./templates/index.html", "is_file": True},
            "/test/get": {"allowed_methods": ["GET"], "content": "./templates/test.html", "is_file": True},
            "/test/post": {"allowed_methods": ["POST"], "content": "./templates/test.html", "is_file": True},
            "/test/put": {"allowed_methods": ["PUT"], "content": "./templates/test.html", "is_file": True},
            "/test/post-get": {"allowed_methods": ["GET", "POST"], "content": "./templates/test.html", "is_file": True},
            "/favicon.ico": {"allowed_methods": ["GET"], "content": "./static/img/favicon.jpg", "is_file": True}
        }

    @log_decorator("REQUEST HANDLING")
    def handle_request(self, request: Request) -> Response:
        logger.debug("[DECIDING PATH] Path: {}".format(request.path))
        route = self.route_lookup.get(request.path)
        if not route:
            logger.error("[404 NOT FOUND] Path: {}".format(request.path))
            return Response(STATUS_NOT_FOUND, "<html><body>404 Route Not Found</body></html>", "text/html")

        logger.debug("[CHECKING METHOD] Allowed Methods: {}".format(', '.join(route["allowed_methods"])))
        if request.method not in route["allowed_methods"]:
            logger.error("[405 METHOD NOT ALLOWED] Method: {}, Allowed: {}".format(request.method, ', '.join(route["allowed_methods"])))
            return Response(
                STATUS_METHOD_NOT_ALLOWED,
                f"<html><body>405 Method Not Allowed: {request.method} not allowed for {request.path}. Allowed: {', '.join(route['allowed_methods'])}</body></html>",
                "text/html"
            )

        if route["is_file"]:
            file_path = route["content"]
            if not os.path.exists(file_path) or os.path.isdir(file_path):
                logger.error("[404 FILE NOT FOUND] File Path: {}".format(file_path))
                return Response(STATUS_NOT_FOUND, "<html><body>404 Resource Not Found</body></html>", "text/html")

            with open(file_path, "rb") as file:
                content = file.read()
            content_type = get_content_type(file_path)
            logger.debug("[FILE SERVED] File Path: {}, Content Type: {}".format(file_path, content_type))
            return Response(STATUS_SUCCESS, content, content_type)

class HttpServer:
    def __init__(self, port, backlog=10):
        self.port = port
        self.backlog = backlog
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind(("0.0.0.0", port))
        self.server_socket.listen(backlog)
        self.request_handler = RequestHandler()

    @log_decorator("SERVER RUNNING")
    def run(self):
        while True:
            client_socket, address = self.server_socket.accept()
            logger.info("[CONNECTION ESTABLISHED] Address: {}".format(address))
            try:
                request_data = client_socket.recv(3000).decode()
                if request_data:
                    request = Request(request_data)
                    response = self.request_handler.handle_request(request)
                    http_response = response.build_response()
                    client_socket.sendall(http_response)
                    logger.info("[RESPONSE SENT] Status Code: {}".format(response.code))
                else:
                    logger.warning("[NO DATA RECEIVED]")
            except Exception as e:
                logger.error("[EXCEPTION] - {}".format(str(e)))
            finally:
                client_socket.close()
                logger.info("[CONNECTION CLOSED]")

if __name__ == "__main__":
    server = HttpServer(8080)
    server.run()
