import http.server
import socketserver
import webbrowser
import threading
import time
import sys
import os
import shutil
import json

PORT = 8000

class QuietHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        # Keep the terminal output clean and readable
        sys.stdout.write(f"[HTTP] {self.address_string()} - {format%args}\n")

    def do_POST(self):
        # Custom endpoint to write live user credentials back to the local PC's config.json
        if self.path == "/api/save-config":
            try:
                content_length = int(self.headers['Content-Length'])
                post_data = self.rfile.read(content_length)
                config_data = json.loads(post_data.decode('utf-8'))
                
                data_dir = os.path.join(os.getcwd(), "data")
                os.makedirs(data_dir, exist_ok=True)
                
                config_path = os.path.join(data_dir, "config.json")
                with open(config_path, "w", encoding="utf-8") as f:
                    json.dump(config_data, f, indent=2)
                
                print(f"[SERVER] Successfully saved live configuration to: data/config.json")
                
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()
                self.wfile.write(json.dumps({"status": "success"}).encode('utf-8'))
            except Exception as e:
                print(f"[SERVER ERROR] Failed to save config.json: {e}")
                self.send_response(500)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"status": "error", "message": str(e)}).encode('utf-8'))
        else:
            self.send_response(404)
            self.end_headers()

def start_server():
    
    handler = QuietHTTPRequestHandler
    # Allow port reuse immediately to prevent "Address already in use" errors
    socketserver.ThreadingTCPServer.allow_reuse_address = True
    
    with socketserver.ThreadingTCPServer(("", PORT), handler) as httpd:
        print(f"\n[SERVER] SUGAROTA LOCAL WEB SERVER STARTED!")
        print(f"==================================================")
        print(f"[URL] Local Web URL: http://localhost:{PORT}/installer.html")
        print(f"[DIR] Served from: current directory")
        print(f"==================================================")
        print(f"Press Ctrl+C in this terminal window to stop the server.\n")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down server...")

def open_browser():
    time.sleep(0.5)
    url = f"http://localhost:{PORT}/installer.html"
    print(f"[BROWSER] Launching default web browser to: {url}")
    webbrowser.open(url)

if __name__ == "__main__":
    headless = "--no-browser" in sys.argv or "--headless" in sys.argv

    server_thread = threading.Thread(target=start_server, daemon=True)
    server_thread.start()
    
    # Start integrated version auto-incrementer background thread
    try:
        import watch_version
        watcher_thread = threading.Thread(target=watch_version.watch, daemon=True)
        watcher_thread.start()
    except Exception as e:
        print(f"[SERVER WARNING] Could not start integrated version watcher: {e}")
    
    if not headless:
        open_browser()
    else:
        print("[BROWSER] Headless/VPS mode active: Automatic browser launch skipped.")
    
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nStopping installer host. Goodbye!")
        sys.exit(0)
