//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="Interfaces.d.ts"/>
/// <reference path="IE11.DiagnosticOM.d.ts" />
/// <reference path="Browser.ts"/>

module Proxy {
    "use strict";

    declare var browser: DiagnosticsOM.IBrowser;
    export class DOMHandler implements IDomainHandler {
        private _mapUidToNode: Map<number, Node>;
        private _mapNodeToUid: WeakMap<Node, number>;
        private _nextAvailableUid: number;
        private _windowExternal: any; // todo: Make an appropriate TS interface for external
        private _elementHighlightColor: any;
        private _nextAvailableStyleSheetUid: number;
        private _sentCSS: boolean; // todo: find a better way to send this
        private _mapStyleSheetToStyleSheetID: WeakMap<StyleSheet, number>;
        private _inspectModeEnabled: boolean;
        private _selectElementHandler: (event:Event) => void;
        private _hoverElementHandler: (event: Event) => void;

        constructor() {
            this._mapUidToNode = new Map<number, Node>();
            this._mapNodeToUid = new WeakMap<Node, number>();
            this._mapStyleSheetToStyleSheetID = new WeakMap<StyleSheet, number>();
            this._nextAvailableUid = 2; // 1 is reserved for the root
            this._nextAvailableStyleSheetUid = 1;
            this._windowExternal = (<any>external);
            this._sentCSS = false;
            this._inspectModeEnabled = false;
            this._elementHighlightColor = {
                margin: "rgba(250, 212, 107, 0.50)",
                border: "rgba(120, 181, 51, 0.50)",
                padding: "rgba(247, 163, 135, 0.50)",
                content: "rgba(168, 221, 246, 0.50)"
            };

            this._selectElementHandler = (event: Event) => {
                this.selectElementHandler(event);
            };

            this._hoverElementHandler = (event: Event) => {
                if (event.target) {
                    this.highlightNode(<Element>event.target);
                }
            };
        }

        public processMessage(method: string, request: IWebKitRequest): void {
            var processedResult: IWebKitResult;
            switch (method) {
                case "getDocument":
                    processedResult = this.getDocument();
                    break;

                case "hideHighlight":
                    // Chrome dev tools never requests CSS files, but always sends "hideHighlight" command around when it expects to get the CSS files.
                    // this hack will do until we implement a better way to post events.
                    if (!this._sentCSS) {
                        this._sentCSS = true;
                        this.styleSheetAdded();
                    }

                    processedResult = this.hideHighlight();
                    break;

                case "highlightNode":
                    processedResult = this.handleHighlightNodeRequest(request);
                    break;

                case "setInspectModeEnabled":
                    processedResult = this.setInspectModeEnabled(request);
                    break;

                case "requestChildNodes":
                    processedResult = this.requestChildNodes(request);
                    break;

                case "getInlineStylesForNode":
                    // todo: Implement this function
                    break;

                case "getMatchedStylesForNode":
                    processedResult = this.getMatchedStylesForNode(request);
                    break;

                case "getComputedStyleForNode":
                    processedResult = this.getComputedStyleForNode(request);
                    break;

                default:
                    processedResult = {};
                    processedResult.result = {};
                    break;
            }

            browserHandler.postResponse(request.id, processedResult);
        }

        private getNodeUid(node: Node): number {
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

        private getNode(nodeUID: number): Node {
            if (!this._mapUidToNode.has(nodeUID)) {
                throw Error("Could not find node with UID " + nodeUID);
            }

            return this._mapUidToNode.get(nodeUID);
        }

        private selectElementHandler(event: Event): void {
            var target: Element = <Element>event.target;
            if (target) {
                this.highlightNode(target);
            }

            var chain: Node[] = [];
            var curentElt: Node = target;

            // expand the dom up to the selected node, starting from the closest parent the Chrome Dev Tools knows about
            while (curentElt.parentNode && !this._mapNodeToUid.has(curentElt)) {
                chain.push(curentElt);
                curentElt = curentElt.parentNode;
            }

            chain.push(curentElt);
            while (chain.length > 0) {
                this.setChildNodes(this.getNodeUid(chain.pop()));
            }

            var response: any = {
                method: "DOM.inspectNodeRequested",
                params: {
                    nodeId: this.getNodeUid(target)
                }
            };

            this._windowExternal.sendMessage("postMessage", JSON.stringify(response));
        }

        private setInspectModeEnabled(request: IWebKitRequest): IWebKitResult {
            if (request.params.enabled && !this._inspectModeEnabled) {
                this._inspectModeEnabled = true;
                diagnostics.browser.elementSelectionEventsEnabled = true;
                browser.addEventListener("selectElement", this._selectElementHandler);
                browser.addEventListener("hoverElement", this._hoverElementHandler);
            } else if (!request.params.enabled && this._inspectModeEnabled) {
                this._inspectModeEnabled = false;
                diagnostics.browser.elementSelectionEventsEnabled = false;
                browser.removeEventListener("selectElement", this._selectElementHandler);
                browser.removeEventListener("hoverElement", this._hoverElementHandler);
            }

            var processedResult: IWebKitResult = { result: {} };
            return processedResult;
        }

        private styleSheetAdded(): void {
            for (var i = 0; i < browser.document.styleSheets.length; i++) {
                var styleSheet: StyleSheet = browser.document.styleSheets[i];
                var ownerid = this.getNodeUid(styleSheet.ownerNode);
                var ssID = this._nextAvailableStyleSheetUid++;
                var sourceURL = styleSheet.href;
                var disabled = styleSheet.disabled;

                this._mapStyleSheetToStyleSheetID.set(styleSheet, ssID);

                var response: any = {};
                response.method = "CSS.styleSheetAdded";
                response.params = {};
                response.params.header = {};
                response.params.header.styleSheetId = "" + ssID;
                response.params.header.origin = "regular"; // todo: see if there is a way to get this data from IE
                response.params.header.disabled = disabled;
                response.params.header.sourceURL = sourceURL;
                response.params.header.title = styleSheet.title;
                response.params.header.frameId = "1500.1"; // todo: add support for Iframes
                response.params.header.isInline = "false"; // todo: see if there is a way to get this data from IE
                response.params.header.startLine = "0";
                response.params.header.startColumn = "0";
                response.params.header.ownerNode = "" + ownerid;

                (<any>external).sendMessage("postMessage", JSON.stringify(response));
            }
        }

        private getJsonFromRule(rule: CSSStyleRule): any {
            var parsedStyleList: DiagnosticsOM.IStylePropertyList = diagnostics.styles.getParsedPropertyList(rule.style);
            var sourceLoc: DiagnosticsOM.ISourceLocation = diagnostics.styles.getSourceLocation(rule.style);

            // Chrome expects different range for selector and properties, and the start+end location for both
            // IE only gives us the start of location of the selector so this is a best approximation
            var rangeObj = { startLine: sourceLoc.line, startColumn: sourceLoc.column, endLine: sourceLoc.line + 1, endColumn: 0 };

            var json_rule: any = {};
            json_rule.rule = {};
            json_rule.rule.selectorList = {};
            json_rule.rule.selectorList.selectors = [({ value: rule.selectorText, range: rangeObj })];
            json_rule.rule.selectorList.text = rule.selectorText;
            json_rule.rule.origin = "regular";
            json_rule.rule.style = {};
            if (parsedStyleList.length > 0) {
                json_rule.rule.style.cssProperties = [];
                for (var i = 0; i < parsedStyleList.length; i++) {
                    json_rule.rule.style.cssProperties.push({ name: parsedStyleList[i].propertyName, value: parsedStyleList[i].value });
                    json_rule.rule.style.cssProperties[i].text = rule.style.cssText;
                    json_rule.rule.style.cssProperties[i].range = rangeObj;
                    json_rule.rule.style.cssProperties[i].implicit = false; // todo: see if there is a way to get this data from IE
                    json_rule.rule.style.cssProperties[i].disabled = false; // todo: see if there is a way to get this data from IE
                }
            }

            json_rule.rule.style.shorthandEntries = []; // todo: see if there is a way to get this data from IE
            json_rule.rule.style.styleSheetId = "" + this._mapStyleSheetToStyleSheetID.get(rule.parentStyleSheet);
            json_rule.rule.style.range = rangeObj;
            json_rule.rule.style.cssText = rule.style.cssText;
            json_rule.rule.styleSheetId = "" + this._mapStyleSheetToStyleSheetID.get(rule.parentStyleSheet);
            json_rule.matchingSelectors = [0]; // todo: see if there is a way to get this data from IE
            return json_rule;
        }

        // getInheritanceChain API only returns a partial inheritence chain containing only nodes that have styles attached to them. 
        // The chrome debugger requires you return an empty "matchedCSSRules" node for elements that do not have styles attached
        // so that it can properly determine where inherited styles came from. This builds the InheritanceChain and inserts 
        // null elements to represent an emlement with no CSS rules.
        private calculateInheritanceChain(htmlElement: HTMLElement): HTMLElement[] {
            diagnostics.styles.calculateTracedStyles(htmlElement);
            var partialChain: HTMLElement[] = diagnostics.styles.getTracedStyles(htmlElement).getInheritanceChain();
            var traceNode: Node = htmlElement.parentElement;
            var fullChain: HTMLElement[] = [];
            var i: number = 0;
            while (i < partialChain.length) {
                if (partialChain[i] === htmlElement) {
                    i++;
                    continue;
                }

                if (traceNode === partialChain[i]) {
                    fullChain.push(partialChain[i++]);
                } else {
                    fullChain.push(null);
                }

                traceNode = traceNode.parentNode;
            }

            return fullChain;
        }

        private getComputedStyleForNode(request: IWebKitRequest): IWebKitResult {
            var processedResult: IWebKitResult = {};
            var node: Node = this.getNode(request.params.nodeId);
            if (!node || node.nodeType !== NodeType.ElementNode) {
                processedResult.error = "could not find element"; // todo: find official error
                return processedResult;
            }

            var htmlElement: HTMLElement = <HTMLElement>node;
            var doc: Document = htmlElement.ownerDocument;
            var window: Window = this.getDefaultView(doc);
            if (!window) {
                processedResult.error = "could not find view for node"; // todo: find official error
                return processedResult;
            }

            var computedStyles: CSSStyleDeclaration = window.getComputedStyle(htmlElement);

            processedResult.result = {};
            processedResult.result.computedStyle = [];
            for (var i = 0; i < computedStyles.length; i++) {
                var propertyName = computedStyles[i];
                var propertyValue = computedStyles.getPropertyValue(propertyName);
                processedResult.result.computedStyle.push({ name: propertyName, value: propertyValue });
            }

            return processedResult;
        }

        // todo: Implement excludePseudo, and excludeInherited arguments
        private getMatchedStylesForNode(request: IWebKitRequest): IWebKitResult {
            var node: Node = this.getNode(request.params.nodeId);
            var processedResult: IWebKitResult = {};
            var rulesEncountered: CSSStyleRule[] = [];
            if (!node || node.nodeType !== NodeType.ElementNode) {
                processedResult.error = "could not find element"; // todo find official error
                return processedResult;
            }

            var htmlElement: HTMLElement = <HTMLElement>node;
            var styleRules: CSSStyleRule[] = this.getStyleRules(htmlElement);

            // first find the styles that are directly applied to this htmlElement.
            processedResult.result = {};
            processedResult.result.matchedCSSRules = [];
            for (var j = 0; j < styleRules.length; j++) {
                var rule: CSSStyleRule = styleRules[j];
                if (rulesEncountered.indexOf(rule) < 0) {
                    rulesEncountered.push(rule);
                    processedResult.result.matchedCSSRules.push(this.getJsonFromRule(rule));
                }
            }

            processedResult.result.pseudoElements = []; // todo: see if there is a way to get this data from IE

            // Now find all the inherited styles on htmlElement
            processedResult.result.inherited = [];
            var chain: HTMLElement[] = this.calculateInheritanceChain(htmlElement);

            for (var i = 0; i < chain.length; i++) {
                var traceElement: HTMLElement = chain[i];
                processedResult.result.inherited.push({ matchedCSSRules: [] });

                if (traceElement === null) {
                    // a null element in the chain indicates that the element has no styles attached to it.
                    // Looking for styles on this element will find styles inherited from parent elements, which
                    // will cause the chrome dev tools to incorrectly display where a style is inherited from.
                    continue;
                }

                var styleRules: CSSStyleRule[] = this.getStyleRules(traceElement);
                for (var j = 0; j < styleRules.length; j++) {
                    var rule: CSSStyleRule = styleRules[j];
                    if (rulesEncountered.indexOf(rule) < 0) {
                        rulesEncountered.push(rule);
                        processedResult.result.inherited[processedResult.result.inherited.length - 1].matchedCSSRules.push(this.getJsonFromRule(rule));
                    }
                }
            }

            return processedResult;
        }

        private createChromeNodeFromIENode(node: Node): INode {
            var iNode: INode = {
                nodeId: this.getNodeUid(node),
                nodeType: node.nodeType,
                nodeName: node.nodeName,
                localName: node.localName || "",
                nodeValue: node.nodeValue || ""
            };

            if (node.nodeType === NodeType.DocumentTypeNode) {
                iNode.publicId = (<DocumentType>node).publicId || "";
                iNode.systemId = (<DocumentType>node).systemId || "";
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

        /**
         * Does the same thing as createChromeNodeFromIENode but also recursively converts child nodes. 
         */
        private createChromeNodeFromIENodeRecursive(ieNode: Node, depth: number): INode {
            var chromeNode: INode = this.createChromeNodeFromIENode(ieNode);

            if (this.numberOfNonWhitespaceChildNodes(ieNode) > 0) {
                chromeNode.childNodeCount = this.numberOfNonWhitespaceChildNodes(ieNode);
            }

            if (depth > 0) {
                if (!chromeNode.children && chromeNode.childNodeCount > 0) {
                    chromeNode.children = [];
                }

                for (var i = 0; i < ieNode.childNodes.length; i++) {
                    if (!this.isWhitespaceNode(ieNode.childNodes[i])) {
                        chromeNode.children.push(this.createChromeNodeFromIENodeRecursive(ieNode.childNodes[i], depth - 1));
                    }
                }
            }

            return chromeNode;
        }

        private isWhitespaceNode(node: Node): boolean {
            return (node.nodeType === NodeType.TextNode && node.nodeValue.trim() === "");
        }

        private numberOfNonWhitespaceChildNodes(node: Node): number {
            var nonWhitespaceChildNodes = 0;
            for (var i = 0; i < node.childNodes.length; i++) {
                if (!this.isWhitespaceNode(node.childNodes[i])) {
                    nonWhitespaceChildNodes++;
                }
            }

            return nonWhitespaceChildNodes;
        }

        private setChildNodes(id: number): any {
            var ieNode: Node = this.getNode(id);
            var nodeArray: INode[] = [];

            // loop over all nodes, ignoring whitespace nodes
            for (var i = 0; i < ieNode.childNodes.length; i++) {
                if (!this.isWhitespaceNode(ieNode.childNodes[i])) {
                    nodeArray.push(this.createChromeNodeFromIENodeRecursive(ieNode.childNodes[i], 1));
                }
            }

            // Send the response back over the websocket
            var response: any = {}; // todo add a type for this. It has no id so it's not an IWebKitResponse
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

            var validChildren: number = this.numberOfNonWhitespaceChildNodes(browser.document);

            if (validChildren > 0) {
                document.childNodeCount = validChildren;
                document.children = [];
            }

            for (var i = 0; i < browser.document.childNodes.length; i++) {
                if (!this.isWhitespaceNode(browser.document.childNodes[i])) {
                    document.children.push(this.createChromeNodeFromIENodeRecursive(browser.document.childNodes[i], 1));
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

        private searchRuleListForStyleObject(ruleList: any, style: CSSStyleDeclaration): any {
            var rule: any;
            for (var i = 0; i < ruleList.length; i++) {
                rule = ruleList[i];
                if (rule.cssRules) {
                    rule = this.searchRuleListForStyleObject(rule.cssRules, style);
                    if (rule) {
                        return rule;
                    }
                } else if (rule.style === style) {
                    return rule;
                }
            }

            return null;
        }

        private searchStyleSheetForStyleObject(sheets: StyleSheetList, style: CSSStyleDeclaration): CSSStyleRule {
            for (var i = 0; i < sheets.length; i++) {
                var sheet = <CSSStyleSheet>sheets[i];
                try {
                    // Use standard CSS OM if available, otherwise fall back to legacy CSS OM
                    var rules = sheet.cssRules ? sheet.cssRules : sheet.rules;
                    var rule: CSSStyleRule;
                    if (rules) {
                        rule = this.searchRuleListForStyleObject(rules, style);
                        if (rule) {
                            return rule;
                        }
                    }

                    if (sheet.imports && sheet.imports.length) {
                        rule = this.searchStyleSheetForStyleObject(sheet.imports, style);
                        if (rule) {
                            return rule;
                        }
                    }
                } catch (ex) {
                    // If the rule references a file that does not exist (or cannot be accessed), sheet.rules will throw an exception
                    // todo: add an error to our response 
                }
            }

            return null;
        }

        private getStyleRules(element: HTMLElement): CSSStyleRule[] {
            var styleRules = <CSSStyleRule[]>[];
            diagnostics.styles.calculateTracedStyles(element);
            var appliedStyles = diagnostics.styles.getTracedStyles(element).getAppliedStyles();

            // The rules are given by the API in winningest first order. To render CSS, we need the reverse.
            for (var i = appliedStyles.length - 1; i >= 0; i--) {
                var styleRule = this.searchStyleSheetForStyleObject(element.ownerDocument.styleSheets, appliedStyles[i]);
                // Inline styles have no parentRule, so we skip them by checking styleRule.
                // They are already captured by copying the attributes above.
                if (styleRule) {
                    styleRules.push(styleRule);
                }
            }

            return styleRules;
        }

        private highlightNode(elementToHighlight: Node): Boolean {
            while (elementToHighlight && elementToHighlight.nodeType !== NodeType.ElementNode) {
                elementToHighlight = elementToHighlight.parentNode;
            }

            if (!elementToHighlight) {
                return false;
            }

            try {
                browser.highlightElement((<Element>elementToHighlight), this._elementHighlightColor.margin, this._elementHighlightColor.border, this._elementHighlightColor.padding, this._elementHighlightColor.content);
            } catch (e) {
                // todo: I have no idea why this randomly fails when you give it the head node, but it does
            }

            return true;
        }

        private handleHighlightNodeRequest(request: IWebKitRequest): IWebKitResult {
            var element_to_highlight: Node = this.getNode(request.params.nodeId);
            var processedResult: IWebKitResult = {};

            if (this.highlightNode(element_to_highlight)) {
                processedResult.result = {};
                return processedResult;
            }

            processedResult.error = "could not find element"; // todo find official error
            return processedResult;
        }

        private requestChildNodes(request: IWebKitRequest): IWebKitResult {
            if (request.params && request.params.nodeId) {
                this.setChildNodes(request.params.nodeId);
            }

            return {};
        }

        private getDefaultView(doc: any): Window {
            if (doc) {
                if (typeof doc.defaultView !== "undefined") {
                    return doc.defaultView;
                } else {
                    return doc.parentWindow;
                }
            }
        
            return null;
        }
    }

    export var domHandler: DOMHandler = new DOMHandler();
}