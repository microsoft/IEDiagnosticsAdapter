//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="IE11.DiagnosticOM.d.ts" />
/// <reference path="Interfaces.d.ts"/>

/// Proxy to hande the page domain of the Chrome remote debug protocol 
module Proxy {
    export class RuntimeHandler implements IDomainHandler {
        constructor() {
        }

        public processMessage(method: string, request: IWebKitRequest): void {
            var processedResult: IWebKitResult;

            switch (method) {
                case "enable":
                    processedResult = { result: {} };
                    break;

                case "evaluate":
                case "callFunctionOn":
                    var resultFromEval: any;
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
                        value = null;
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
                    processedResult = null;
                    break;
            }

            browserHandler.postResponse(request.id, processedResult);
        }
    }

    export var runtimeHandler: RuntimeHandler = new RuntimeHandler();
}