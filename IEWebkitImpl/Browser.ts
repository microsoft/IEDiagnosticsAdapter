//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="Interfaces.d.ts"/>

module F12.Proxy {
    "use strict";

    declare var host: any; //todo: create some interface for host
    declare var request: any; //todo: create some interface for request

    declare var browser: IBrowser;

    class BrowserHandler {
        externalObj: any;
        constructor() {
            this.externalObj = external; //todo: Make a better interface for external
            this.externalObj.addEventListener("message", (e: any) => this.messageHandler(e));
        }

        private PostResponse(id: number, value: any) {
            // Send the response back over the websocket
            var response: IWebKitResponse = Common.CreateResponse(id, value);
            this.externalObj.sendMessage("postMessage", JSON.stringify(response));
        }

        private PostNotification(method: any, params: any) {
            var notification = {
                method: method,
                params: params
            }

        this.externalObj.sendMessage("postMessage", JSON.stringify(notification));
        }

        private ProcessRuntime(method: any, request: any) {
            var processedResult;

            switch (method) {
                case "evaluate":
                case "callFunctionOn":
                    var resultFromEval = undefined;
                    var wasThrown = false;

                    if (method === "evaluate" && request.params.expression) {
                        try {
                            var escapedInput = JSON.stringify(request.params.expression).slice(1, -1);
                            resultFromEval = browser.executeScript(escapedInput);
                        } catch (e) {
                            resultFromEval = e;
                            wasThrown = true;
                        }

                    } else if (method === "callFunctionOn" && request.params.functionDeclaration) {
                        var args = [];
                        if (request.params.arguments) {
                            args.push(" ");
                            for (var i = 0; i < request.params.arguments.length; i++) {
                                var arg = request.params.arguments[i].value;
                                args.push(JSON.stringify(arg));
                            }
                        }

                        try {
                            var command = request.params.functionDeclaration + ".call(window" + args.join(",") + ")";
                            var escapedInput = JSON.stringify(command).slice(1, -1);
                            resultFromEval = browser.executeScript(escapedInput);
                        } catch (e) {
                            resultFromEval = e;
                            wasThrown = true;
                        }
                    }

                    var id = null;
                    var description = (resultFromEval ? resultFromEval.toString() : "");
                    var value = resultFromEval;

                    if (resultFromEval && typeof resultFromEval === "object") {
                        id = "1";
                        description = "Object";
                        value = undefined;
                    }

                    var resultDesc = {
                        objectId: id,
                        type: "" + typeof value,
                        value: value,
                        description: description
                    };

                    processedResult = {
                        result: {
                            wasThrown: wasThrown,
                            result: resultDesc
                        }
                    };

                    break;

                default:
                    processedResult = {};
                    break;
            }

            this.PostResponse(request.id, processedResult);
        }

        private ProcessPage(method: any, request: any) {
            var processedResult;

            switch (method) {
                case "navigate":
                    if (request.params.url) {
                        //browser.document.parentWindow.alert(browser.document.location.href + " -> " + request.params.url);
                        browser.executeScript("window.location.href = '" + request.params.url + "'");

                        processedResult = {
                            result: {
                                frameId: 5000.1
                            }
                        };
                    }
                    break;

                default:
                    processedResult = {};
                    break;
            }

            this.PostResponse(request.id, processedResult);
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

                    //browser.document.parentWindow.alert(e.data);

                    switch (methodParts[0]) {
                        case "Runtime":
                            this.ProcessRuntime(methodParts[1], request);
                            break;

                        case "Page":
                            this.ProcessPage(methodParts[1], request);
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
    var browserHandler = new BrowserHandler();
}