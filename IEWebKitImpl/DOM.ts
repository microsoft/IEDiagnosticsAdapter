//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="Interfaces.d.ts"/>
/// <reference path="DiagnosticOM.d.ts" />
/// <reference path="Browser.ts"/>

module F12.Proxy {
    "use strict";

    declare var browser: DiagnosticsOM.IBrowser;
    export class DOMHandler implements IDomainHandler {
        private _mapUidToNode: Map<number, Node>;
        private _mapNodeToUid: WeakMap<Node, number>;
        private _nextAvailableUid: number;
        private _windowExternal: any; //todo: Make an appropriate TS interface for external
        private _elementHighlightColor: any;

        constructor() {
            this._mapUidToNode = new Map<number, Node>();
            this._mapNodeToUid = new WeakMap<Node, number>();
            this._nextAvailableUid = 2; // 1 is reserved for the root
            this._windowExternal = (<any>external);
            this._elementHighlightColor = {
                margin: "rgba(250, 212, 107, 0.50)",
                border: "rgba(120, 181, 51, 0.50)",
                padding: "rgba(247, 163, 135, 0.50)",
                content: "rgba(168, 221, 246, 0.50)"
            };
        }

        private createChromeNodeFromIENode(node: Node): INode {
            var iNode: INode = {
                nodeId: this.getOrAssignUid(node),
                nodeType: node.nodeType,
                nodeName: node.nodeName,
                localName: browser.document.localName || "",
                nodeValue: browser.document.nodeValue || "",

            };

            if (node.childNodes.length > 0) {
                iNode.childNodeCount = node.childNodes.length;
            }

            if (node.attributes) {
                iNode.attributes = [];
                for (var i = 0; i < node.attributes.length; i++) {
                    iNode.attributes.push(node.attributes[i].name);
                    iNode.attributes.push(node.attributes[i].value);
                }
            }

            return iNode;
        }

        public getOrAssignUid(node: Node): number {
            if (!node) {
                throw new Error("invalid node");
            }

            if (node === browser.document) {
                return 1;
            }

            var uid: number;
            if (this._mapNodeToUid.has(node)) {
                return this._mapNodeToUid.get(node);
            }

            uid = this._nextAvailableUid++;
            this._mapUidToNode.set(uid, node);
            this._mapNodeToUid.set(node, uid);
            return uid;
        }

         /**
          * Does the same thing as createChromeNodeFromIENode but also recursively converts child nodes. 
          */
        private createChromeNodeFromIENodeRecursive(ieNode: Node): INode {
            //todo: add depth limitation

            var chromeNode: INode = this.createChromeNodeFromIENode(ieNode);
            if (!chromeNode.children && chromeNode.childNodeCount > 0) {
                chromeNode.children = [];
            }

            for (var i = 0; i < ieNode.childNodes.length; i++) {
                if (ieNode.childNodes[i].nodeType === NodeType.ELEMENT_NODE) {
                    chromeNode.children.push(this.createChromeNodeFromIENodeRecursive(ieNode.childNodes[i]));
                }
            }
            return chromeNode;
        }

        private setChildNodes(id: number): any {
            var ieNode: Node = this._mapUidToNode.get(id);
            var chromeNode = this.createChromeNodeFromIENode(ieNode);
            var nodeArray: INode[] = []
            for (var i = 0; i < ieNode.childNodes.length; i++) {
                nodeArray.push(this.createChromeNodeFromIENodeRecursive(ieNode.childNodes[i]));
            }

            // Send the response back over the websocket
            var response: any = {}; // todo add a type for this. It has no id so its not an IWebKitResponse
            response.method = "DOM.setChildNodes";
            response.params = {};
            response.params.parentId = id;
            response.params.nodes = nodeArray;
            this._windowExternal.sendMessage("postMessage", JSON.stringify(response));

            return {}; // actual response to setChildNodes is empty.
        }

        private getDocument(): IWebKitResult {
            var document: INode = {
                nodeId: 1,
                nodeType: browser.document.nodeType,
                nodeName: browser.document.nodeName,
                localName: browser.document.localName || "",
                nodeValue: browser.document.nodeValue || "",
                documentURL: browser.document.URL,
                baseURL: browser.document.URL, // fixme: this line or the above line is probably not right
                xmlVersion: browser.document.xmlVersion,
            };

            if (!this._mapUidToNode.has(1)) {
                this._mapUidToNode.set(1, browser.document);
                this._mapNodeToUid.set(browser.document, 1);
            }

            if (browser.document.childNodes.length > 0) {
                document.childNodeCount = browser.document.childNodes.length;
                document.children = [];
            }

            for (var i = 0; i < browser.document.childNodes.length; i++) {
                if (browser.document.childNodes[i].nodeType === NodeType.ELEMENT_NODE) {
                    document.children.push(this.createChromeNodeFromIENodeRecursive(browser.document.childNodes[i]));
                }
            }

            var processedResult = {
                result: {
                    root: document
                }
            };
            return processedResult;
        }

        private hideHighlight(): IWebKitResult {
            browser.highlightElement(null, "", "", "", ""); // removes highlight from all elements.
            return {};
        }

        private highlightNode(request: IWebKitRequest): IWebKitResult {
            var element_to_highlight: Node = this._mapUidToNode.get(request.params.nodeId);
            while (element_to_highlight && element_to_highlight.nodeType != NodeType.ELEMENT_NODE) {
                element_to_highlight = element_to_highlight.parentNode;
            }
            if (element_to_highlight) {
                try {
                    browser.highlightElement((<Element>element_to_highlight), this._elementHighlightColor.margin, this._elementHighlightColor.border, this._elementHighlightColor.padding, this._elementHighlightColor.content);
                } catch (e) {
                    // todo: I have no idea why this randomly fails when you give it the head node, but it does
                }
                return {}
            } else {
                var processedResult: any = {};
                processedResult.error = "could not find element"; //todo find official error
                return processedResult;
            }


        }
        private requestChildNodes(request: IWebKitRequest): IWebKitResult {
            if (request.params && request.params.nodeId) {
                this.setChildNodes(request.params.nodeId);
            }
            return {};
        }

        public processMessage(method: string, request: IWebKitRequest): void {
            var processedResult;

            switch (method) {
                case "getDocument":
                    processedResult = this.getDocument();
                    break;
                case "hideHighlight":
                    processedResult = this.hideHighlight();
                    break;
                case "highlightNode":
                    processedResult = this.highlightNode(request);
                    break;
                case "requestChildNodes":
                    processedResult = this.requestChildNodes(request);
                    break;
                default:
                    processedResult = {};
                    break;
            }

            browserHandler.PostResponse(request.id, processedResult);
        }
    }

    export var domHandler: DOMHandler = new DOMHandler();
}