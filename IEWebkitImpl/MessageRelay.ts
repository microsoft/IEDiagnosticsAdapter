//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="Interfaces.d.ts"/>

module F12.Proxy {
    declare var host: any; //todo: create some interface for host

    // The MessageRelay exists to send messages along to the appropriate javascript engine
    class MessageRelay {
        constructor() {
            host.addEventListener("onmessage", (data: any) => this.onmessageHandler(data));
        }

        private PostResponse(id: number, value: any) {
            // Send the response back over the websocket
            var response: messageResponce = {
                id: id
            };

            if (value.error) {
                response.error = value.error;
            }

            if (value.result) {
                response.result = value.result;
            }

            host.postMessage(JSON.stringify(response));
        }

        private onmessageHandler(data: any): void {
            // Try to parse the requested command
            var request = null;
            try {
                request = JSON.parse(data);
            } catch (ex) {
                this.PostResponse(0, {
                    error: { description: "Invalid request" }
                });
                return;
            }

            // Process a successful request on the correct thread
            if (request) {
                var methodParts = request.method.split(".");
                if (methodParts[0].toLowerCase() == "debugger") {
                    host.postMessageToEngine("debugger", data);
                } else {
                    host.postMessageToEngine("browser", data);
                }
            }
        }
    }

    var webSocket = new MessageRelay();
}