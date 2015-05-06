//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="Interfaces.d.ts"/>
/// <reference path="IE11.DiagnosticOM.d.ts" />

module Proxy {
    "use strict";

    declare var host: any; // todo: create some interface for host
    declare var request: any; // todo: create some interface for request

    declare var browser: DiagnosticsOM.IBrowser;
    export class BrowserHandler {
        private _windowExternal: any; // todo: Make an appropriate TS interface for external

        constructor() {
            this._windowExternal = (<any>external);
            this._windowExternal.addEventListener("message", (e: any) => this.messageHandler(e));
        }

        public postResponse(id: number, value: IWebKitResult): void {
            // Send the response back over the websocket
            var response: IWebKitResponse = Common.createResponse(id, value);
            this._windowExternal.sendMessage("postMessage", JSON.stringify(response));
        }

        private alert(message: string): void {
            this._windowExternal.sendMessage("alert", message);
        }

        private postNotification(method: string, params: any): void {
            var notification = {
                method: method,
                params: params
            };

            this._windowExternal.sendMessage("postMessage", JSON.stringify(notification)); // todo: should this be postMessage?
        }

        private messageHandler(e: any): void {
            if (e.id === "onmessage") {
                // Try to parse the requested command
                var request = null;
                try {
                    request = JSON.parse(e.data);
                } catch (ex) {
                    this.postResponse(0, {
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
                        case "CSS":
                            domHandler.processMessage(methodParts[1], request);
                            break;

                        case "Worker":
                            if (methodParts[1] === "canInspectWorkers") {
                                var processedResult: IWebKitResult = { result: false };
                                browserHandler.postResponse(request.id, processedResult);
                            }

                            break;

                        default:
                            this.postResponse(request.id, {});
                            break;
                    }
                }
            } else if (e.id === "onnavigation") {
                this.postNotification("Page.frameNavigated", {
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