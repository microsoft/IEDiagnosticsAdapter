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
        windowExternal: any; //todo: Make an appropriate TS interface for external

        constructor() {
            this._mapUidToNode = new Map<number, Node>();
            this._mapNodeToUid = new WeakMap<Node, number>();
            this._nextAvailableUid = 44; // 43 is reserved for root to copy chrome, maybe change this to 2 later
            this.windowExternal = (<any>external);
            this.windowExternal.addEventListener("message", (e: any) => this.messageHandler(e));
        }

        private alert(message: string): void {
            this.windowExternal.sendMessage("alert", message);
        }

        private PostResponse(id: number, value: IWebKitResult) {
            // Send the response back over the websocket
            var response: IWebKitResponse = Common.CreateResponse(id, value);
            var debughelper = JSON.stringify(response); //todo : remove this
            this.windowExternal.sendMessage("postMessage", JSON.stringify(response));
        }

        private PostNotification(method: string, params: any) {
            var notification = {
                method: method,
                params: params
            }

            this.windowExternal.sendMessage("postMessage", JSON.stringify(notification)); //todo: should this be postMessage?
        }

        private ProcessRuntime(method: string, request: IWebKitRequest) {
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

        private ProcessPage(method: string, request: IWebKitRequest) {
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
        ///BELOW: DOM STUFF
        private _mapUidToNode: Map<number, Node>;
        private _mapNodeToUid: WeakMap<Node, number>;
        private _nextAvailableUid: number;

        private nodeToINode(node: Node): INode {

            var inode: INode = {
                nodeId: +this.getOrAssignUid(node),
                nodeType: node.nodeType,
                nodeName: node.nodeName || "",
                localName: browser.document.localName || "",
                nodeValue: browser.document.nodeValue || "",
                childNodeCount: node.childNodes.length
            };
            inode.attributes = [];
            if (node.attributes) {
                for (var i = 0; i < node.attributes.length; i++) {
                    inode.attributes.push(node.attributes[i].value); //todo: ensure this returns the same array as chrome
                    inode.attributes.push(node.attributes[i].value);
                }
            }

            return inode;
        }

        private popKidsRecursive(inode: INode, node: Node, childNum: number, depth: number): INode { //fixme childNum is not needed
            var newchild: INode = this.nodeToINode(node);
            if (!inode.children) {
                inode.children = [];
            }
            inode.children.push(newchild);
            if (depth > 0) {
                for (var i = 0; i < newchild.childNodeCount; i++) {
                    this.popKidsRecursive(newchild, node.childNodes[i], i, depth - 1);
                }
            }
            return inode;
        }

        public getOrAssignUid(node: Node): number {
            if (!node) {
                return;
            }

            if (node === browser.document) {
                return 1;
            }
            var uid: number;
            //var uid = (<HTMLElement>node).uniqueID;
            //if (uid) {
            // if (this.getNode(uid)) { // <-- should ALWAYS succeed.
            //      return uid;
            //   }
            // needs mapping then continue with this id.
            //}

            if (this._mapNodeToUid.has(node)) {
                return this._mapNodeToUid.get(node);
            }

            uid = uid || this._nextAvailableUid++;

            this._mapUidToNode.set(uid, node);
            this._mapNodeToUid.set(node, uid);
            return uid;
        }
        /*
        public getNode(uid: string): Node {
            if (uid === "#root") {
                return browser.document;
            }

            var node: Node = this._dom.getElementByUniqueId(uid);
            if (node) {
                if (!this.isNodeAccessible(node)) {
                    // Should never happen.
                    return null;
                }

                return node;
            }

            node = this._mapUidToNode.get(uid);
            if (!node) {
                return null;
            }

            if (!this.isNodeAccessible(node)) {
                this._mapUidToNode.delete(uid);
                return null;
            }

            return node;
        }*/

        private popKidsRecursiveTry2(iEnode: Node): INode { //fixme childNum is not needed
            var chromeNode: INode = this.nodeToINode(iEnode);
            if (!chromeNode.children && chromeNode.childNodeCount > 0) {
                chromeNode.children = [];
            }
            //todo: add an assert iEnode.childNodes.length == chromeNode.childNodeCount 
            for (var i = 0; i < iEnode.childNodes.length; i++) {
                chromeNode.children.push(this.popKidsRecursiveTry2(iEnode.childNodes[i]));
            }

            return chromeNode;
        }

        private setChildNodes(id: number): void {
            var iEnode: Node = this._mapUidToNode.get(id);
            var chromeNode = this.nodeToINode(iEnode);
            var nodeArray: INode[] = []
            for (var i = 0; i < iEnode.childNodes.length; i++) {
                nodeArray.push(this.popKidsRecursiveTry2(iEnode.childNodes[i]));
            }


            // Send the response back over the websocket
            var response: any = {}; // todo type this. it has no ide so its not an Iwebkitresponce
            response.method = "DOM.setChildNodes";
            response.params = {};
            response.params.parentId = id;
            response.params.nodes = nodeArray;
            var debughelper = JSON.stringify(response); //todo : remove this
            this.windowExternal.sendMessage("postMessage", JSON.stringify(response));
        }

        private ProcessDOM(method: string, request: IWebKitRequest): void {
            var processedResult;

            switch (method) {
                //todo pull out into files/funcions
                case "getDocument":
                    //var id:string =.toString();
                    var x: INode = {
                        nodeId: 43, //+browser.document.uniqueID, // unary + to convert string to number fixme
                        nodeType: browser.document.nodeType,
                        nodeName: browser.document.nodeName,
                        localName: browser.document.localName || "",
                        nodeValue: browser.document.nodeValue || "",
                        documentURL: browser.document.URL,
                        baseURL: browser.document.URL, // fixme: this line or the above line is probaly not right
                        xmlVersion: browser.document.xmlVersion,
                        childNodeCount: browser.document.childNodes.length
                    };


                    for (var i = 0; i < browser.document.childNodes.length; i++){
                        this.popKidsRecursive(x, browser.document.childNodes[i],i, 2);
                    }


                    //browser.document.
                    processedResult = {
                        result: {
                            root: x
                        }
                    };

                    break;
                case "hideHighlight":
                    processedResult = {}
                    break;

                case "highlightNode":
                    processedResult = {}
                    break;

                case "requestChildNodes":
                    if (request.params && request.params.nodeId) { //fixme this is probally unneeded
                        //var nodeId: number = ;
                        this.setChildNodes(request.params.nodeId);
                    }


                    processedResult = {};
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
                        case "DOM":
                            this.ProcessDOM(methodParts[1], request);
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