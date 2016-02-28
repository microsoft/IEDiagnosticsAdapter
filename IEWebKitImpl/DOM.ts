//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="Interfaces.d.ts"/>
/// <reference path="IE11.DiagnosticOM.d.ts" />
/// <reference path="Browser.ts"/>
/// <reference path="CSSParser.ts"/>

module IEDiagnosticsAdapter {
    "use strict";

    declare var browser: DiagnosticsOM.IBrowser;
    export class DOMHandler implements IDomainHandler {
        private _mapUidToNode: Map<number, Node>;
        private _mapNodeToUid: WeakMap<Node, number>;
        private _mapNodeIdToInlineStyleSheetId: Map<number, string>;
        private _mapInlineStyleSheetIdToNodeId: Map<string, number>;
        private _nextAvailableUid: number;
        private _firstValidStyleSheetUid: number;
        private _windowExternal: any; // todo: Make an appropriate TS interface for external
        private _elementHighlightColor: any;
        private _nextAvailableStyleSheetUid: number;
        private _sentCSS: Set<Document>;
        private _waitingOnGetDocumentRequestId: number; 

        // This keeps track of which nodes the Chrome Dev tools knows about. Any ID in _sentNodeIds the Chrome tools know about and know the parent chain up to the root document.
        // This is needed for the inspect element button so we can find the closet parent to the inspected element that the chrome tools know about.
        private _sentNodeIds: Set<number>;
        private _mapStyleSheetToStyleSheetID: WeakMap<CSSStyleSheet, string>;
        private _mapStyleSheetIDToStyleSheet: Map<string, CSSStyleSheet>;

        // todo: Eventually we want to get rid of this and only use the actual stylesheet. However, that causes issues with disabled properties, 
        // because there won't be any text in the style sheet and returning a property with lengh 0 is weird
        private _mapStyleSheetIDToStyleSheetText: Map<string, string>; 
        private _inspectModeEnabled: boolean;
        private _selectElementHandler: (event: Event) => void;
        private _hoverElementHandler: (event: Event) => void;

        constructor() {
            this._mapUidToNode = new Map<number, Node>(); // todo: This keeps nodes alive, which causes a memmory leak if the website is trying to remove nodes
            this._mapNodeToUid = new WeakMap<Node, number>();
            this._mapNodeIdToInlineStyleSheetId = new Map<number, string>();
            this._mapInlineStyleSheetIdToNodeId = new Map<string, number>();
            this._sentCSS = new Set<Document>();
            this._sentNodeIds = new Set<number>();
            this._waitingOnGetDocumentRequestId = 0;
            this._mapStyleSheetToStyleSheetID = new WeakMap<CSSStyleSheet, string>();
            this._mapStyleSheetIDToStyleSheet = new Map<string, CSSStyleSheet>();
            this._mapStyleSheetIDToStyleSheetText = new Map<string, string>();
            this._nextAvailableUid = 1;
            this._nextAvailableStyleSheetUid = 1;
            this._firstValidStyleSheetUid = 1;
            this._windowExternal = (<any>external);
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

            browser.addEventListener("documentComplete", (dispatchWindow: any) => {
                // if _waitingOnGetDocumentRequestId is not zero, it means we got a getDocument request from  the chrome dev tools before the document was finished loading. 
                // Now that we have the document we can fufill the request
                if (this._waitingOnGetDocumentRequestId !== 0) {
                    var processedResult: IWebKitResult = this.getDocument();
                    browserHandler.postResponse(this._waitingOnGetDocumentRequestId, processedResult);
                    this._waitingOnGetDocumentRequestId = 0;
                }
            });
        }

        public processMessage(method: string, request: IWebKitRequest): void {
            var processedResult: IWebKitResult;
            switch (method) {
                case "getDocument":
                    if (!browser.document || !browser.document.body) { // when the page closes browser.document can be "unspecified error"
                        // when we navigate, we need to send the documentUpdated notification before we get any scriptParsed messages.
                        // because of this, sometimes chrome requests the new document before it is finished loading, in this case set a flag and respond once we get a document complete message.
                        this._waitingOnGetDocumentRequestId = request.id;
                        return;
                    }

                    processedResult = this.getDocument();
                    break;

                case "hideHighlight":
                    processedResult = this.hideHighlight();
                    break;

                case "getAttributes":
                    processedResult = this.getAttributes(request);
                    break;

                case "setPropertyText":
                    processedResult = this.setPropertyText(request);
                    break;

                case "getStyleSheetText":
                    processedResult = this.getStyleSheetText(request);
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
                    if (!this._mapUidToNode.has(request.params.nodeId)) {
                        // when we refresh the page, sometimes the chrome dev tools ask for css on a nodeID that was invalidated. 
                        // They also do this when the normal chrome Dev Tools are attached, so detect the problem and send back the same error chrome does.
                        // The three separate error messages for the three different CSS requests are copy-pasted from chrome
                        processedResult = { error: { code: -32000, message: "No node with given id found" } };
                        break;
                    }

                    processedResult = this.getInlineStylesForNode(request);
                   
                    break;

                case "getMatchedStylesForNode":
                    if (!this._mapUidToNode.has(request.params.nodeId)) {
                        processedResult = { error: { code: -32000, message: "Node not found" } };
                        break;
                    }

                    processedResult = this.getMatchedStylesForNode(request);
                    break;

                case "getComputedStyleForNode":
                    if (!this._mapUidToNode.has(request.params.nodeId)) {
                        processedResult = processedResult = { error: { code: -32000, message: "No node with given id found" } };
                        break;
                    }

                    processedResult = this.getComputedStyleForNode(request);
                    break;

                case "pushNodesByBackendIdsToFrontend":
                    processedResult = this.pushNodesByBackendIdsToFrontend(request);
                    break;

                case "pushNodeByPathToFrontend":
                    // for now this seems to only be called when you navigate, and it only requests document.body.
                    // if we see this being used in other situations we will need an actual implimentaiton
                    processedResult = {
                        result: {
                            nodeId: this.getNodeUid(browser.document.body)
                        }
                    };
                    break;

                default:
                    processedResult = {};
                    processedResult.result = {};
                    break;
            }

            browserHandler.postResponse(request.id, processedResult);
        }

        public resetState(): void {
            this.onNavigate(/*sendNotifications=*/false);
            this._waitingOnGetDocumentRequestId = 0;
            this._nextAvailableUid = 1;
            this._nextAvailableStyleSheetUid = 1;
            this._firstValidStyleSheetUid = 1;
        }

        public onNavigate(sendNotifications: boolean = true): void {
            for (var i = this._firstValidStyleSheetUid; i < this._nextAvailableStyleSheetUid; i++) {
                if (sendNotifications) {
                    browserHandler.postNotification("CSS.styleSheetRemoved", { styleSheetId: "" + i });
                }
            }

            this._firstValidStyleSheetUid = this._nextAvailableStyleSheetUid + 1;

            if (sendNotifications) {
                browserHandler.postNotification("Console.messagesCleared", null);
                browserHandler.postNotification("Debugger.globalObjectCleared", null);
            }

            // Since we have navigated, all of the stored information about nodes and CSS is no longer valid, so clear our state.
            this._mapUidToNode = new Map<number, Node>();
            this._mapNodeToUid = new WeakMap<Node, number>();
            this._mapInlineStyleSheetIdToNodeId = new Map<string, number>();
            this._mapNodeIdToInlineStyleSheetId = new Map<number, string>();

            this._sentCSS = new Set<Document>();
            this._sentNodeIds = new Set<number>();
            this._mapStyleSheetToStyleSheetID = new WeakMap<CSSStyleSheet, string>();
            this._mapStyleSheetIDToStyleSheet = new Map<string, CSSStyleSheet>();
            this._mapStyleSheetIDToStyleSheetText = new Map<string, string>();

            if (sendNotifications) {
                this.styleSheetAdded(browser.document); // send cssAdded notificaitons
                browserHandler.postNotification("DOM.documentUpdated", null);
            }
        }

        private getNodeUid(node: Node): number {
            if (!node) {
                throw new Error("invalid node");
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

        private pushNodesByBackendIdsToFrontend(request: IWebKitRequest): IWebKitResult {
            var processedResult: IWebKitResult = {};

            var targetId: number = request.params.backendNodeIds[0];
            Assert.hasValue(targetId);
            var target: Element = <Element>this.getNode(targetId);

            // expand the dom up to the selected node, starting from the closest parent the Chrome Dev Tools knows about
            var chain: Node[] = this.findParentChainForElement(target).reverse();

            // chain[0] is the root document and [length-1] is target. We want to send setChildNodes notifications starting from the closest parent of target that chrome knows about
            var startIndex: number = chain.length;
            for (var i = 0; i < chain.length; i++) {
                if (!this._sentNodeIds.has(this.getNodeUid(chain[i]))) {
                    startIndex = i - 1;
                    break;
                }
            }

            if (startIndex !== chain.length) {
                chain.pop(); // we don't want to send a setChildNodes notification for the selected element, only it's parents
                for (var i = startIndex; i < chain.length; i++) {
                    this.setChildNodes(this.getNodeUid(chain[i]));
                }
            }

            processedResult = {
                result: {
                    nodeIds: [targetId]
                }
            };
            return processedResult;
        }

        private selectElementHandler(event: Event): void {
            var target: Element = <Element>event.target;
            if (target) {
                this.highlightNode(target);
            }

            var response: any = {
                method: "DOM.inspectNodeRequested",
                params: {
                    backendNodeId: this.getNodeUid(target)
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

        private getStyleSheetText(request: IWebKitRequest): IWebKitResult {
            var styleSheetId: string = request.params.styleSheetId;
            Assert.isTrue(this._mapStyleSheetIDToStyleSheet.has(styleSheetId), "styleSheet does not exist");

            var cssText = this.getCssText(styleSheetId);

            var processedResult: IWebKitResult = {
                result: {
                    text: cssText
                }
            };

            // We can't actually update the CSSText in _mapStyleSheetIDToStyleSheetText to refect the real CSStext here, because we may still
            // get edits based on line/column returned in getMatchedStylesForNode or a prior edit
            return processedResult;
        }

        private setPropertyText(request: IWebKitRequest): IWebKitResult {
            var styleSheetId: string = request.params.styleSheetId;
            var text: string = request.params.text;
            var range: IRange = request.params.range;

            var cssText = this.getCssText(styleSheetId);
            var startoffset = this.getOffsetfromLineCol(cssText, range.startLine, range.startColumn);
            var endoffset = this.getOffsetfromLineCol(cssText, range.endLine, range.endColumn);
            cssText = cssText.substring(0, startoffset) + text + cssText.substring(endoffset);
            this._mapStyleSheetIDToStyleSheetText.set(styleSheetId, cssText);

            var styleSheet = this._mapStyleSheetIDToStyleSheet.get(styleSheetId);

            // Chrome stores inline styles as a Stylesheet, We can't actualy refrence that styleSheet because it does not exist in IE
            if (!styleSheet) { // handle the case of editing an "inline style sheet" 
                var node = this.getNode(this._mapInlineStyleSheetIdToNodeId.get(styleSheetId));
                var styleAttribute = browser.document.createAttribute("style");
                styleAttribute.value = cssText;
                node.attributes.setNamedItem(styleAttribute);

                // send attributeModified notification 
                var attributeModifiedParams = {
                    nodeId: this._mapInlineStyleSheetIdToNodeId.get(styleSheetId),
                    name: "style",
                    value: cssText
                };
                browserHandler.postNotification("DOM.attributeModified", attributeModifiedParams);

                modifiedRule = new CssParser(cssText).parseInlineCss();
            } else { // this case is for normal styleSheets
                // This line does not actually set styleSheet.cssText equal to our modified CSSText, instead Trident parses the new css and constructs stylesheet.CSSText from the rules/properties it detects
                // in particular, this means if we disable a property, rather an appearing commented out, that property will dissapear from styleSheet.cssText
                styleSheet.cssText = cssText; 

                var parsedCss = new CssParser(cssText).parseCss();
                var modifiedRuleOffset = this.getOffsetfromLineCol(cssText, range.startLine, range.startColumn);
                for (var i = 0; i < parsedCss.length; i++) {
                    if (parsedCss[i].originalOffset <= modifiedRuleOffset && parsedCss[i].endOffset >= modifiedRuleOffset) {
                        var modifiedRule = parsedCss[i];
                        break;
                    }
                }
            }

            Assert.hasValue(modifiedRule, "supplied offset is not part of a rule");
            var cssProperties = this.convertParsedCSSToJSONObject(modifiedRule.declarations, cssText);

            // send cssStyleSheetChanged notification 
            var styleSheetChangedParams = {
                styleSheetId: styleSheetId
            };
            browserHandler.postNotification("CSS.styleSheetChanged", styleSheetChangedParams);

            var ruleStart = this.getLineColFromOffset(cssText, modifiedRule.originalOffset);
            var ruleEnd = this.getLineColFromOffset(cssText, modifiedRule.endOffset);

            Assert.isTrue(modifiedRule.declarations.length > 0, "Editing rules with no valid styles is not yet supported");

            var styleCssText = cssText.substring(modifiedRule.declarations[0].originalOffset, modifiedRule.declarations[modifiedRule.declarations.length - 1].endOffset);
            var processedResult: IWebKitResult = {
                result: {
                    style: {
                        cssProperties: cssProperties,
                        shorthandEntries: [],
                        styleSheetId: styleSheetId,
                        range: {
                            startLine: ruleStart.line,
                            endLine: ruleEnd.line,
                            startColumn: ruleStart.column,
                            endColumn: ruleEnd.column
                        },
                        cssText: styleCssText
                    }
                }
            };

            return processedResult;
        }

        // todo: this is already done in debugger.ts, merge the two functions
        private getLineColFromOffset(text: string, offset: number): ILineCol {
            if (offset === 0) {
                return { line: 0, column: 0 };
            }

            Assert.isTrue(offset <= text.length, "invalid offset");
            var line = 0;
            var column = 0;
            for (var i = 0; i < offset; i++) {
                ++column;

                if (text[i] === "\r" && text[i + 1] === "\n") { // "\r\n" is a single line break, so only count it once
                    i++;
                }

                if (text[i] === "\r" || text[i] === "\n") {
                    column = 0;
                    ++line;
                }
            }

            return {
                line: line,
                column: column
            };
        }

        private getOffsetfromLineCol(text: string, line: number, col: number): number {
            var curLine = 0;
            var curCol = 0;
            for (var i = 0; i < text.length; i++) {
                if (curCol === col && curLine === line) {
                    return i;
                }

                ++curCol;
                if (text[i] === "\r" && text[i + 1] === "\n") { // "\r\n" is a single line break, so only count it once
                    i++;
                }

                if (text[i] === "\r" || text[i] === "\n") {
                    curCol = 0;
                    ++curLine;
                }
            }

            if (curCol === col && curLine === line) {
                return text.length; // one past the end is a valid offset as long as it will be used as an ending offset
            }

            Assert.fail("Invalid Line and Column");
            return -1;
        }

        private styleSheetAdded(doc: Document): void {
            this._sentCSS.add(doc);
            for (var i = 0; i < doc.styleSheets.length; i++) {
                var styleSheet: CSSStyleSheet = <CSSStyleSheet>doc.styleSheets[i]; // todo: if non-css stylesheets exist hanlde it somehow
                var styleSheetID: string = "" + this._nextAvailableStyleSheetUid++;
                this._mapStyleSheetToStyleSheetID.set(styleSheet, styleSheetID);
                this._mapStyleSheetIDToStyleSheet.set(styleSheetID, styleSheet);

                var response: any = {
                    method: "CSS.styleSheetAdded",
                    params: {
                        header: {
                            styleSheetId: "" + styleSheetID,
                            origin: "regular", // todo: see if there is a way to get this data from IE
                            disabled: styleSheet.disabled,
                            sourceURL: styleSheet.href,
                            title: styleSheet.title,
                            frameId: Common.getiframeId(doc),
                            isInline: "false", // todo: see if there is a way to get this data from IE
                            startLine: "0",
                            startColumn: "0",
                            ownerNode: "" + this.getNodeUid(styleSheet.ownerNode),
                        }
                    }
                };

                (<any>external).sendMessage("postMessage", JSON.stringify(response));
            }
        }

        private getJsonFromRule(rule: CSSStyleRule): any {
            var parsedStyleList: DiagnosticsOM.IStylePropertyList = diagnostics.styles.getParsedPropertyList(rule.style);
            var styleSheet = rule.parentStyleSheet;

            Assert.isTrue(this._mapStyleSheetToStyleSheetID.has(rule.parentStyleSheet), "could not find styleSheetId");
            var styleSheetId = this._mapStyleSheetToStyleSheetID.get(rule.parentStyleSheet);
            var cssText = this.getCssText(styleSheetId);

            var parser = new CssParser(rule.parentStyleSheet.cssText);
            var parsed = parser.parseCss();
            for (var i = 0; i < styleSheet.rules.length; i++) {
                if (styleSheet.rules[i] === rule) {
                    var ruleindex = i;
                }
            }

            // Chrome expects different range for selector and properties, and the start+end location for both
            // IE only gives us the start of location of the selector so this is a best approximation
            var startLineCol = this.getLineColFromOffset(cssText, parsed[ruleindex].originalOffset);
            var endLineCol = this.getLineColFromOffset(cssText, parsed[ruleindex].endOffset);

            // todo fixme: check to make sure json parsed correctly, fill in garbage otherwise
            var ruleRange = { startLine: startLineCol.line, startColumn: startLineCol.column, endLine: endLineCol.line, endColumn: endLineCol.column };

            var jsonRule: any = {
                rule: {
                    selectorList: {
                        selectors: [({ value: rule.selectorText, range: ruleRange })],
                        text: rule.selectorText
                    },
                    origin: "regular",
                    style: {
                        shorthandEntries: [], // todo: see if there is a way to get this data from IE
                        styleSheetId: styleSheetId,
                        range: ruleRange,
                        cssText: cssText
                    },
                    styleSheetId: styleSheetId
                },
                matchingSelectors: [0] // todo: see if there is a way to get this data from IE
            };

            if (parsedStyleList.length > 0) {
                jsonRule.rule.style.cssProperties = this.convertParsedCSSToJSONObject(parsed[ruleindex].declarations, cssText);
            }

            return jsonRule;
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
            var window: Window = Common.getDefaultView(doc);
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

        private getAttributes(request: IWebKitRequest): IWebKitResult {
            var nodeId: number = request.params.nodeId;

            var node: HTMLElement = <HTMLElement>this.getNode(nodeId);
            var attributes: string[] = [];
            for (var i = 0; i < node.attributes.length; i++) {
                var attribute = node.attributes.item(i);
                attributes.push(attribute.name);
                attributes.push(attribute.value);
            }

            var processedResult: IWebKitResult = {
                result: {
                    attributes: attributes
                }
            };

            return processedResult;
        }

        private getInlineStylesForNode(request: IWebKitRequest): IWebKitResult {
            var nodeId = request.params.nodeId;
            var styleSheetId: string;
            if (!this._mapNodeIdToInlineStyleSheetId.has(nodeId)) {
                styleSheetId = "" + this._nextAvailableStyleSheetUid++;
                this._mapInlineStyleSheetIdToNodeId.set(styleSheetId, nodeId);
                this._mapNodeIdToInlineStyleSheetId.set(nodeId, styleSheetId);
            } else {
                styleSheetId = this._mapNodeIdToInlineStyleSheetId.get(request.params.nodeId);
            }
            
            var cssText = this.getCssText(styleSheetId);
            var parsed = new CssParser(cssText).parseInlineCss();
            var cssProperties = this.convertParsedCSSToJSONObject(parsed.declarations, cssText);
            var endLineCol = this.getLineColFromOffset(cssText, cssText.length);

            var processedResult: IWebKitResult = {
                result: {
                    inlineStyle: {
                        cssProperties: cssProperties,
                        shorthandEntries: [],
                        styleSheetId: styleSheetId,
                        range: {
                            startLine: 0,
                            startColumn: 0,
                            endLine: endLineCol.line,
                            endColumn: endLineCol.column,
                        },
                        cssText: cssText
                    }
                }
            };

            return processedResult;
        }

        private convertParsedCSSToJSONObject(declarations: ICssDeclaration[], cssText: string): any {
            return declarations.map((property: ICssDeclaration) => {
                var rowCol = this.getLineColFromOffset(cssText, property.originalOffset);
                var endRowCol = this.getLineColFromOffset(cssText, property.endOffset);
                return {
                    name: property.property,
                    value: property.value,
                    text: cssText.substring(property.originalOffset, property.endOffset), // this does not seem quite right
                    implicit: false,
                    disabled: property.isDisabled,
                    range: {
                        startLine: rowCol.line,
                        endLine: endRowCol.line,
                        startColumn: rowCol.column,
                        endColumn: endRowCol.column
                    }
                };
            });
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
       
        /**
         * Converts the nodes that exist in Internet Explorer to nodes that the Chrome Dev tools understand
         */
        private createChromeNodeFromIENode(ieNode: Node, depth: number): INode {
            var chromeNode: INode = {
                nodeId: this.getNodeUid(ieNode),
                nodeType: ieNode.nodeType,
                nodeName: ieNode.nodeName,
                localName: ieNode.localName || "",
                nodeValue: ieNode.nodeValue || ""
            };

            if (ieNode.nodeType === NodeType.DocumentTypeNode) {
                chromeNode.publicId = (<DocumentType>ieNode).publicId || "";
                chromeNode.systemId = (<DocumentType>ieNode).systemId || "";
            }

            if (ieNode.attributes) {
                chromeNode.attributes = [];
                for (var i = 0; i < ieNode.attributes.length; i++) {
                    chromeNode.attributes.push(ieNode.attributes[i].name);
                    chromeNode.attributes.push(ieNode.attributes[i].value);
                }
            }

            // if the element is an iframe
            if ((<any>ieNode).contentWindow) {
                var doc = ieNode;
                while (doc.parentNode) {
                    doc = doc.parentNode;
                }

                var response: IgetValidWindowResponse = Common.getValidWindow((<Document>doc).defaultView, (<HTMLFrameElement>ieNode).contentWindow);
                if (response.isValid) {
                    var frameDoc: Document = response.window.document;
                    if (!this._sentCSS.has(frameDoc)) {
                        this.styleSheetAdded(frameDoc);
                    }

                    chromeNode.frameId = Common.getiframeId(frameDoc);
                    chromeNode.contentDocument = {
                        nodeId: this.getNodeUid(frameDoc),
                        nodeType: frameDoc.nodeType,
                        nodeName: frameDoc.nodeName,
                        localName: frameDoc.localName,
                        nodeValue: frameDoc.nodeValue,
                        documentURL: frameDoc.URL,
                        baseUrl: frameDoc.URL,
                        xmlVersion: frameDoc.xmlVersion,
                        childNodeCount: this.numberOfNonWhitespaceChildNodes(frameDoc)
                    };
                }
            }

            // currently any time createChromeNodeFromIENode is called, we are sending the resault to the chrome dev tools and the chrome dev tools know the entire parent chain from chromeNode to the root document
            // if this changes in the future, we will need to only add this id to _sentNodeIds if we are telling chrome about the node.
            this._sentNodeIds.add(this.getNodeUid(ieNode));

            // now recusively add children to chromeNode
            if (this.numberOfNonWhitespaceChildNodes(ieNode) > 0) {
                chromeNode.childNodeCount = this.numberOfNonWhitespaceChildNodes(ieNode);
            }

            if (depth > 0) {
                if (!chromeNode.children && chromeNode.childNodeCount > 0) {
                    chromeNode.children = [];
                }

                for (var i = 0; i < ieNode.childNodes.length; i++) {
                    if (!this.isWhitespaceNode(ieNode.childNodes[i])) {
                        chromeNode.children.push(this.createChromeNodeFromIENode(ieNode.childNodes[i], depth - 1));
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
            this._sentNodeIds.add(id);
            // loop over all nodes, ignoring whitespace nodes
            for (var i = 0; i < ieNode.childNodes.length; i++) {
                if (!this.isWhitespaceNode(ieNode.childNodes[i])) {
                    nodeArray.push(this.createChromeNodeFromIENode(ieNode.childNodes[i], 1));
                }
            }

            // Send the response back over the websocket
            var setChildNodesParams = {
                parentId: id,
                nodes: nodeArray
            };
            browserHandler.postNotification("DOM.setChildNodes", setChildNodesParams);

            return {}; // actual response to setChildNodes is empty.
        }

        private getDocument(): IWebKitResult {
            this.styleSheetAdded(browser.document);
            var nodeID = this.getNodeUid(browser.document);
            var document: INode = {
                nodeId: nodeID,
                nodeType: browser.document.nodeType,
                nodeName: browser.document.nodeName,
                localName: browser.document.localName || "",
                nodeValue: browser.document.nodeValue || "",
                documentURL: browser.document.URL,
                baseUrl: browser.document.URL, // fixme: this line or the above line is probably not right
                xmlVersion: browser.document.xmlVersion,
            };

            this._sentNodeIds.add(nodeID);
            var validChildren: number = this.numberOfNonWhitespaceChildNodes(browser.document);

            if (validChildren > 0) {
                document.childNodeCount = validChildren;
                document.children = [];
            }

            for (var i = 0; i < browser.document.childNodes.length; i++) {
                if (!this.isWhitespaceNode(browser.document.childNodes[i])) {
                    document.children.push(this.createChromeNodeFromIENode(browser.document.childNodes[i], 1));
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

        private getCssText(styleSheetId: string): string {
            if (this._mapStyleSheetIDToStyleSheetText.has(styleSheetId)) {
                return this._mapStyleSheetIDToStyleSheetText.get(styleSheetId);
            }

            var styleSheet = this._mapStyleSheetIDToStyleSheet.get(styleSheetId);
            if (styleSheet) {
                return styleSheet.cssText;
            }

            return "";
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

        private findParentChainForElement(element: Element): Node[] {
            try {
                var partialChain: Element[] = [element];
                if (Common.getDefaultView(element.ownerDocument) !== Common.getDefaultView(browser.document)) {
                    // get the chain of Iframes leading to element
                    var iframeChain: Element[] = this.getIFrameChain(browser.document, element.ownerDocument);                    
                    if (iframeChain && iframeChain.length > 0) {
                        partialChain = partialChain.concat(iframeChain);
                    }
                }

                // partialChain only contains element and iframes nodes, fill in all of the rest of the nodes
                var fullChain: Node[] = [];
                var curentElt: Node;
                for (var i = 0; i < partialChain.length; i++) {
                    curentElt = partialChain[i];
                    while (curentElt) {
                        fullChain.push(curentElt);
                        curentElt = curentElt.parentNode;
                    }
                }

                return fullChain;
            } catch (e) {
                // Unable to find chain
                return [];
            }
        }

        /**
         * Find all the 'iframe' children for this document
         * @param rootDocumemnt The document to start searching in
         * @param findDocument The document the chain should get to
         */
        private getIFrameChain(rootDocument: Document, findDocument: Document): Element[] {
            var tags = rootDocument.querySelectorAll("iframe, frame");
            for (var i = 0, n = tags.length; i < n; i++) {
                // Get a safe window
                var frame = <HTMLIFrameElement>tags[i];
                var view = Common.getDefaultView(rootDocument);
                var result: IgetValidWindowResponse = Common.getValidWindow(view, frame.contentWindow);
                if (result.isValid) {
                    // Compare the documents
                    if (result.window.document === findDocument) {
                        // Found the 'iframe', so return the result
                        return [<Element>tags[i]];
                    }

                    // No match, so 'recurse' into the children 'iframes'
                    var chain = this.getIFrameChain(result.window.document, findDocument);
                    if (chain && chain.length > 0) {
                        // As we unwind the stack, append each 'iframe' element to the chain
                        chain.push(<Element>tags[i]);
                        return chain;
                    }
                }
            }

            // Nothing found
            return [];
        }
    }

    export var domHandler: DOMHandler = new DOMHandler();
}