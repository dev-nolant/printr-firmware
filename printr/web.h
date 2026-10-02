// Local HTTP API and web UI (port 80).
//
//   GET  /                 web UI
//   GET  /api/status       device, WiFi, printer and cloud status (public)
//   POST /api/print        print {"text": "...", "from": "..."} or a text/plain body
//   GET  /api/settings     current settings                      (admin)
//   POST /api/settings     change settings (partial JSON object)  (admin)
//   POST /api/action/<a>   test, chartest, poll, heartbeat, register,
//                          markall, portal, reboot, forget-wifi, factory-reset (admin)
//   POST /api/ca           PEM root certificate for a custom server; empty removes (admin)
//   GET  /update           firmware upload                         (admin)
//   POST /send             print a text/plain body (older alias of /api/print)
//   GET  /status           alias of /api/status
//
// "admin" = HTTP basic auth, user "admin", password = settings.adminPassword.
// Printing is open to the LAN unless requireAuthToPrint is set. Requests from
// other websites (cross-origin browser requests) are always refused, so a web
// page you visit can't print to or reconfigure the printer.
#pragma once

namespace web {

void begin();
void loop();
void start();  // (re)start listening; no-op if already running
void stop();   // release port 80 for the WiFi setup portal

}  // namespace web
