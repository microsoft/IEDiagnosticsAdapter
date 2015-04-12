//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="Interfaces.d.ts"/>
/// <reference path="DiagnosticOM.d.ts" />
/// <reference path="DOM.ts"/>
/// <reference path="page.ts" />
/// <reference path="runtime.ts" />

module F12.Proxy {
    "use strict";

    declare var host: any; //todo: create some interface for host
    declare var request: any; //todo: create some interface for request

    declare var browser: DiagnosticsOM.IBrowser;
    export class BrowserHandler {
        private windowExternal: any; //todo: Make an appropriate TS interface for external

        constructor() {
            this.windowExternal = (<any>external);
            this.windowExternal.addEventListener("message", (e: any) => this.messageHandler(e));
        }

        private alert(message: string): void {
            this.windowExternal.sendMessage("alert", message);
        }

        private PostNotification(method: string, params: any) {
            var notification = {
                method: method,
                params: params
            }

            this.windowExternal.sendMessage("postMessage", JSON.stringify(notification)); //todo: should this be postMessage?
        }

        public PostResponse(id: number, value: IWebKitResult) {
            // Send the response back over the websocket
            var response: IWebKitResponse = Common.CreateResponse(id, value);
            this.windowExternal.sendMessage("postMessage", JSON.stringify(response));
        }

        private messageHandler(e: any) {
            if (e.id === "onmessage") {
                // Try to parse the requested command
                var request = null;
                try {
                    request = JSON.parse(e.data);
                } catch (ex) {
                    this.PostResponse(0, {
                        error: { description: "Invalid request" }
                    });
                    return;
                }

                // Process a successful request on the correct thread
                if (request) {
                    var methodParts = request.method.split(".");

                    // browser.document.parentWindow.alert(e.data);

                    switch (methodParts[0]) {
                        case "Runtime":
                            runtimeHandler.processMessage(methodParts[1], request);
                            break;

                        case "Page":
                            pageHandler.processMessage(methodParts[1], request);
                            break;

                        case "DOM":
                            domHandler.processMessage(methodParts[1], request);
                            break;

                        default:
                            this.PostResponse(request.id, {});
                            break;
                    }
                }
            } else if (e.id === "onnavigation") {
                this.PostNotification("Page.frameNavigated", {
                    frame: {
                        id: "1500.1",
                        url: browser.document.location.href,
                        mimeType: (<any>browser.document).contentType,
                        securityOrigin: (<any>browser.document.location).origin
                    }
                });
            }
        }
    }

    export var browserHandler: BrowserHandler = new BrowserHandler();
}