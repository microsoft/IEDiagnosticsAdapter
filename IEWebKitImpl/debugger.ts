//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="Interfaces.d.ts"/>
/// <reference path="Common.ts"/>

module IEDiagnosticsAdapter {
    "use strict";

    enum BreakResumeAction {
        Abort,
        Continue,
        StepInto,
        StepOver,
        StepOut,
        Ignore, // Continue, but with state (fast refresh)
        StepDocument // Step to document boundary for JMC
    }

    enum ConnectionResult {
        Succeeded = 0,
        Failed = 1,
        FailedAlreadyAttached = 2
    }

    enum BreakReason {
        Step,                // User stepped
        Breakpoint,          // Hit explicit breakpoint
        DebuggerBlock,       // Debugger is breaking another thread
        HostInitiated,       // Host (e.g. mshtml) initiated a breakpoint - not currently used
        LanguageInitiated,   // "debugger;"
        DebuggerHalt,        // Pause button
        Error,               // Exception
        Jit,                 // JIT dialog
        MutationBreakpoint   // Hit mutation breakpoint
    }

    enum MutationType {
        None = 0,
        Update = 1,
        Delete = 1 << 1,
        All = Update | Delete
    }

    enum CauseBreakAction {
        BreakOnAny = 0,
        BreakOnAnyNewWorkerStarting = 1,
        BreakIntoSpecificWorker = 2,
        UnsetBreakOnAnyNewWorkerStarting = 3
    }

    interface ISourceLocation {
        docId: number;
        start: number;
        length: number;
    }

    interface IStackFrame {
        callFrameId: number;
        functionName: string;
        isInTryBlock: boolean;
        isInternal: boolean;
        location: ISourceLocation;
    }

    interface IGetSourceTextResult {
        loadFailed: boolean;
        text: string;
    }

    interface IBreakpointInfo {
        location: ISourceLocation;
        eventTypes: string[];
        breakpointId: string;
        isBound: boolean;
        isEnabled?: boolean;
        condition?: string;
        isTracepoint?: boolean;
        failed?: boolean;
        isPseudoBreakpoint?: boolean;
    }

    interface IResolvedBreakpointInfo {
        breakpointId: number;
        newDocId: number;
        start: number;
        length: number;
        isBound: boolean;
    }

    interface IBreakEventInfo {
        firstFrameId: number;
        errorId: number;
        breakReason: BreakReason;
        description: string;
        isFirstChanceException: boolean;
        isUserUnhandled: boolean;
        breakpoints?: IBreakpointInfo[];
        systemThreadId?: number;
        breakEventType: string;
        mutationBreakpointId: number;
        mutationType: MutationType;
    }

    interface IDocument {
        docId: number;
        parentDocId: number;
        url: string;
        mimeType: string;
        length: number;
        isDynamicCode: boolean;
        headers: string[];
        sourceMapUrlFromHeader: string;
        longDocumentId: number;
    }

    interface ILocation {
        lineNumber: number;
        columnNumber?: number;
        scriptId: string;
    }

    interface IPropertyInfo {
        propertyId: string;
        name: string;
        type: string;
        fullName: string;
        value: string;
        expandable: boolean;
        readOnly: boolean;
        fake: boolean;
        invalid: boolean;
        returnValue: boolean;
    }

    interface IPropertyInfoContainer {
        propInfos: IPropertyInfo[];
        hasAdditionalChildren: boolean;
    }

    interface ISetMutationBreakpointResult {
        success: boolean;
        breakpointId: any;
        objectName: string;
    }

    interface IDebuggerDispatch {
        addEventListener(type: string, listener: Function): void;
        addEventListener(type: "onAddDocuments", listener: (documents: IDocument[]) => void): void;
        addEventListener(type: "onRemoveDocuments", listener: (docIds: number[]) => void): void;
        addEventListener(type: "onUpdateDocuments", listener: (documents: IDocument[]) => void): void;
        addEventListener(type: "onResolveBreakpoints", listener: (breakpoints: IResolvedBreakpointInfo[]) => void): void;
        addEventListener(type: "onBreak", listener: (breakEventInfo: IBreakEventInfo) => void): void;
        removeEventListener(type: string, listener: Function): void;
        removeEventListener(type: "onAddDocuments", listener: (documents: IDocument[]) => void): void;
        removeEventListener(type: "onRemoveDocuments", listener: (docIds: number[]) => void): void;
        removeEventListener(type: "onUpdateDocuments", listener: (documents: IDocument[]) => void): void;
        removeEventListener(type: "onResolveBreakpoints", listener: (breakpoints: IResolvedBreakpointInfo[]) => void): void;
        removeEventListener(type: "onBreak", listener: (breakEventInfo: IBreakEventInfo) => void): void;
        enable(): boolean;
        disable(): boolean;
        isEnabled(): boolean;
        connect(enable: boolean): ConnectionResult;
        disconnect(): boolean;
        shutdown(): boolean;
        isConnected(): boolean;

        causeBreak(causeBreakAction: CauseBreakAction, workerId: number): boolean;
        resume(breakResumeAction: BreakResumeAction): boolean;

        addCodeBreakpoint(docId: number, start: number, condition: string, isTracepoint: boolean): IBreakpointInfo;
        addEventBreakpoint(eventTypes: string[], isEnabled: boolean, condition: string, isTracepoint: boolean): IBreakpointInfo;
        addPendingBreakpoint(url: string, start: number, condition: string, isEnabled: boolean, isTracepoint: boolean): number;
        removeBreakpoint(breakpointId: number): boolean;
        updateBreakpoint(breakpointId: number, condition: string, isTracepoint: boolean): boolean;
        setBreakpointEnabledState(breakpointId: number, enable: boolean): boolean;
        getBreakpointIdFromSourceLocation(docId: number, start: number): number;

        getThreadDescription(): string;
        getThreads(): string[];
        getFrames(framesNeeded: number): IStackFrame[];
        getSourceText(docId: number): IGetSourceTextResult;
        getLocals(frameId: number): number; /* propertyNum */
        eval(frameId: number, evalString: string): IPropertyInfo;
        getChildProperties(propertyId: number, start: number, length: number): IPropertyInfoContainer;
        setPropertyValueAsString(propertyId: number, value: string): boolean;

        canSetNextStatement(docId: number, position: number): boolean;
        setNextStatement(docId: number, position: number): boolean;
        setBreakOnFirstChanceExceptions(value: boolean): boolean;

        canSetMutationBreakpoint(propertyId: number, setOnObject: boolean, mutationType: MutationType): boolean;
        setMutationBreakpoint(propertyId: number, setOnObject: boolean, mutationType: MutationType): ISetMutationBreakpointResult;
        deleteMutationBreakpoint(breakpointId: number): boolean;
        setMutationBreakpointEnabledState(breakpointId: number, enabled: boolean): boolean;
    }

    interface IWebKitPropResult {
        wasThrown: boolean;
        result: IWebKitRemoteObject;
    }

    declare var host: IProxyDebuggerDispatch;
    declare var debug: IDebuggerDispatch;

    class DebuggerProxy {
        private _debugger: IDebuggerDispatch;
        private _isAtBreakpoint: boolean;
        private _isAwaitingDebuggerEnableCall: boolean;
        private _isEnabled: boolean;
        private _documentMap: Map<string, number>;
        private _lineEndingsMap: Map<number, number[]>;
        private _intellisenseExpression: string;
        private _intellisenseFrame: any;

        constructor() {
            this._debugger = debug;
            this._isAtBreakpoint = false;
            this._documentMap = new Map<string, number>();
            this._lineEndingsMap = new Map<number, number[]>();
            this._intellisenseFrame = null;
            this._intellisenseExpression = "";

            // Hook up notifications
            this._debugger.addEventListener("onAddDocuments", (documents: IDocument[]) => this.onAddDocuments(documents));
            this._debugger.addEventListener("onRemoveDocuments", (docIds: number[]) => this.onRemoveDocuments(docIds));
            this._debugger.addEventListener("onUpdateDocuments", (documents: IDocument[]) => this.onUpdateDocuments(documents));
            this._debugger.addEventListener("onResolveBreakpoints", (breakpoints: IResolvedBreakpointInfo[]) => this.onResolveBreakpoints(breakpoints));
            this._debugger.addEventListener("onBreak", (breakEventInfo: IBreakEventInfo) => this.onBreak(breakEventInfo));

            host.addEventListener("onmessage", (data: string) => this.onMessage(data));
        }

        private onMessage(data: string): void {
            // Try to parse the requested command
            var request: IWebKitRequest = null;
            try {
                request = <IWebKitRequest>JSON.parse(data);
            } catch (ex) {
                this.postResponse(0, {
                    error: { description: "Invalid request" }
                });
                return;
            }

            // Process a successful request
            if (request) {
                var methodParts = request.method.split(".");

                if (!this._isAtBreakpoint && methodParts[0] !== "Debugger" && methodParts[0] !== "Custom") {
                    return host.postMessageToEngine("browser", this._isAtBreakpoint, JSON.stringify(request));
                }

                switch (methodParts[0]) {
                    case "Runtime":
                        this.processRuntime(methodParts[1], request);
                        break;

                    case "Debugger":
                        this.processDebugger(methodParts[1], request);
                        break;

                    case "Custom":
                        this.processCustom(methodParts[1], request);
                        break;

                    default:
                        return host.postMessageToEngine("browser", this._isAtBreakpoint, JSON.stringify(request));
                }
            }
        }

        private getLineEndings(docId: number, text?: string): number[] {
            if (!this._lineEndingsMap.has(docId)) {
                var textResult = text || this._debugger.getSourceText(docId).text;
                if (textResult) {
                    var total = [];
                    var lines = textResult.split(/\r\n|\n|\r/);
                    for (var i = 0; i < lines.length; i++) {
                        total.push(lines[i].length + 2);
                    }

                    this._lineEndingsMap.set(docId, total);
                } else {
                    this._lineEndingsMap.set(docId, [0]);
                }
            }

            return this._lineEndingsMap.get(docId);
        }

        private getLocationFromOffset(docId: number, offset: number): ILocation {
            var lineEndings = this.getLineEndings(docId);

            var columnNumber = 0;
            var lineNumber = 0;
            var charCount = 0;
            for (var i = 0; i < lineEndings.length; i++) {
                charCount += lineEndings[i];
                if (offset < charCount) {
                    lineNumber = i;
                    columnNumber = charCount - offset;
                    break;
                }
            }

            return { lineNumber: lineNumber, columnNumber: columnNumber, scriptId: "" + docId };
        }

        private getRemoteObjectFromProp(prop: IPropertyInfo): IWebKitPropResult {
            var type = prop.type.toLowerCase();
            var subType = null;
            var value: any;
            var index = type.indexOf(",");
            if (index !== -1) {
                type = "object";
                subType = type.substring(index + 2).toLowerCase();
            }

            if (subType === "function") {
                type = "function";
                subType = null;
            }

            if (type === "null") {
                type = "object";
                subType = "null";
            }

            if (type === "object" && prop.value === "undefined") {
                type = "undefined";
                subType = null;
            }

            var wasThrown = false;
            if (type === "error") {
                type = "object";
                wasThrown = true;
            }

            if (type === "number") {
                value = parseFloat(prop.value);
            }

            if (typeof prop.value === "string" && prop.value.length > 2 && prop.value.indexOf("\"") === 0 && prop.value.lastIndexOf("\"") === prop.value.length - 1) {
                prop.value = prop.value.substring(1, prop.value.length - 1);
            }

            var resultDesc = {
                objectId: (prop.expandable ? "" + prop.propertyId : null),
                type: type,
                value: (typeof value !== "undefined" ? value : prop.value),
                description: prop.value.toString()
            };

            if (type === "object") {
                (<any>resultDesc).className = "Object";
                (<any>resultDesc).subType = subType;
            }

            return {
                wasThrown: wasThrown,
                result: resultDesc
            };
        }

        private postResponse(id: number, value: IWebKitResult): void {
            // Send the response back over the websocket
            var response: IWebKitResponse = Common.createResponse(id, value);
            host.postMessage(JSON.stringify(response));
        }

        private postNotification(method: string, params: any): void {
            var notification: IWebKitNotification = {
                method: method,
                params: params
            };

            host.postMessage(JSON.stringify(notification));
        }

        private callFunctionOn(request: IWebKitRequest): IWebKitResult {
            if (this._intellisenseFrame && this._intellisenseExpression) {
                var prop = this._debugger.eval(this._intellisenseFrame, this._intellisenseExpression);
                this._intellisenseExpression = "";
                this._intellisenseFrame = null;

                if (!prop) {
                    return {
                        error: "Could not find object"
                    };
                }

                var childProps = this._debugger.getChildProperties(<any>prop.propertyId, 0, 0);
                var value = {};
                for (var i = 0; i < childProps.propInfos.length; i++) {
                    var childProp: IPropertyInfo = childProps.propInfos[i];
                    if (!childProp.fake) {
                        value[childProp.name] = true;
                    }
                }

                return {
                    result: {
                        result: {
                            type: "object",
                            value: value
                        },
                        wasThrown: false
                    }
                };
            }
        }

        private processRuntime(method: string, request: IWebKitRequest): void {
            var processedResult: IWebKitResult;

            switch (method) {
                // copy-pasta from runtime.ts, why is this is two places???
                case "enable":
                    processedResult = { result: {} };
                    break;

                case "callFunctionOn":
                    processedResult = this.callFunctionOn(request);
                    break;

                case "evaluate":
                    var prop = debug.eval(request.params.contextId, request.params.expression);
                    if (prop) {
                        processedResult = {
                            result: this.getRemoteObjectFromProp(prop)
                        };
                    }

                    break;

                case "getProperties":
                    var id = parseInt(request.params.objectId);
                    var props = this._debugger.getChildProperties(id, 0, 0);

                    var viewAccessorOnly = request.params.accessorPropertiesOnly;

                    var propDescriptions = [];
                    for (var i = 0; i < props.propInfos.length; i++) {
                        var prop = props.propInfos[i];
                        if (!prop.fake) {
                            if (typeof viewAccessorOnly !== "undefined") {
                                if (viewAccessorOnly && !prop.readOnly) {
                                    continue;
                                } else if (!viewAccessorOnly && prop.readOnly) {
                                    continue;
                                }
                            }

                            var remote = this.getRemoteObjectFromProp(prop);

                            propDescriptions.push({
                                name: prop.name,
                                value: remote.result,
                                wasThrown: remote.wasThrown
                            });
                        }
                    }

                    processedResult = {
                        result: {
                            result: propDescriptions
                        }
                    };
                    break;

                default:
                    processedResult = {};
                    break;
            }

            this.postResponse(request.id, processedResult);
        }

        private processCustom(method: string, request: IWebKitRequest): void {
            switch (method) {
                case "toolsDisconnected":
                    this.debuggerResume(BreakResumeAction.Continue);
                    return host.postMessageToEngine("browser", this._isAtBreakpoint, "{\"method\":\"Custom.toolsDisconnected\"}");
                    this._debugger.disconnect();
                    this._isEnabled = false;
                    break;

                case "testResetState":
                    this.debuggerResume(BreakResumeAction.Continue);
                    return host.postMessageToEngine("browser", this._isAtBreakpoint, "{\"method\":\"Custom.testResetState\"}");
                    break;
            }
        }

        private processDebugger(method: string, request: IWebKitRequest): void {
            var processedResult: IWebKitResult;

            switch (method) {
                case "canSetScriptSource":
                    processedResult = {
                        result: {
                            result: false
                        }
                    };
                    break;

                case "continueToLocation":
                    break;

                case "disable":
                    this.debuggerResume(BreakResumeAction.Continue);
                    break;

                case "enable":
                    this.debuggerEnable(request.id);
                    return;

                case "evaluateOnCallFrame":
                    // Intelisense from the chrome dev tools calls this than runtime.callFunctionOn. 
                    // We need to return information on the object when runtime.callFunctionOn is called, so save state we will need now
                    if (request.params.objectGroup === "completion") {
                        this._intellisenseExpression = request.params.expression;
                        this._intellisenseFrame = request.params.callFrameId;
                    }

                    var frameId = parseInt(request.params.callFrameId);
                    var prop = this._debugger.eval(frameId, request.params.expression);
                    if (prop) {
                        processedResult = {
                            result: this.getRemoteObjectFromProp(prop)
                        };
                    }

                    break;

                case "getScriptSource":
                    var docId: number = parseInt(request.params.scriptId);
                    var textResult = this._debugger.getSourceText(docId);
                    if (!textResult.loadFailed) {
                        this.getLineEndings(docId, textResult.text);
                        processedResult = {
                            result: {
                                scriptSource: " " + textResult.text
                            }
                        };
                    }

                    break;

                case "pause":
                    this._debugger.causeBreak(CauseBreakAction.BreakOnAny, 0);
                    break;

                case "removeBreakpoint":
                    var bpId = parseInt(request.params.breakpointId);
                    this._debugger.removeBreakpoint(bpId);
                    break;

                case "resume":
                    this.debuggerResume(BreakResumeAction.Continue);
                    break;

                case "searchInContent":
                    break;

                case "setBreakpoint":
                    break;

                case "setBreakpointByUrl":
                    if (this._documentMap.has(request.params.url)) {
                        try {
                            var docId: number = this._documentMap.get(request.params.url);

                            var lineEndings = this.getLineEndings(docId);

                            var charCount = 0;
                            for (var i = 0; i < request.params.lineNumber; i++) {
                                charCount += lineEndings[i];
                            }

                            charCount += request.params.columnNumber;

                            var info = this._debugger.addCodeBreakpoint(docId, charCount, request.params.condition, false);
                        var location = this.getLocationFromOffset(docId, info.location.start);

                            processedResult = {
                                result: {
                                    breakpointId: "" + info.breakpointId,
                                    locations: [location]
                                }
                            };
                        } catch (ex) {
                            this.postResponse(request.id, {
                                error: { description: "Invalid request" }
                            });
                            return;
                        }
                    } else {
                        processedResult = {
                            error: { description: "Not implemented" }
                        };
                    }

                    break;

                case "setBreakpointsActive":
                    break;

                case "setPauseOnExceptions":
                    break;

                case "setScriptSource":
                    break;

                case "stepInto":
                    this.debuggerResume(BreakResumeAction.StepInto);
                    break;

                case "stepOut":
                    this.debuggerResume(BreakResumeAction.StepOut);
                    break;

                case "stepOver":
                    this.debuggerResume(BreakResumeAction.StepOver);
                    break;

                default:
                    processedResult = {};
                    break;
            }

            if (!processedResult) {
                processedResult = {};
            }

            this.postResponse(request.id, processedResult);
        }

        private debuggerEnable(id: number): void {
            if (!this._isEnabled && !this._isAwaitingDebuggerEnableCall) {
                var listener = (succeeded: boolean) => {
                    this._debugger.removeEventListener("debuggingenabled", listener);
                    this._isAwaitingDebuggerEnableCall = false;
                    if (succeeded) {
                        // Now that we have enabled debugging, try to connect to the target
                        var connectionResult = this._debugger.connect(/*enabled=*/ true);

                        if (connectionResult === ConnectionResult.Succeeded) {
                            this._isEnabled = true;
                        }
                    }

                    this.postResponse(id, { result: {} });
                };
                this._debugger.addEventListener("debuggingenabled", listener);

                // This call is asynchronous as it needs to go across threads
                this._isAwaitingDebuggerEnableCall = true;
                this._debugger.enable();
            } else {
                // Already connected, so return success
                this.postResponse(id, { result: {} });
            }
        }

        private debuggerResume(action: BreakResumeAction): void {
            this._debugger.resume(action);
            this._isAtBreakpoint = false;
        }

        private onAddDocuments(documents: IDocument[]): void {
            for (var i = 0; i < documents.length; i++) {
                var document: IDocument = documents[i];
                this._documentMap.set(document.url, document.docId);

                this.postNotification("Debugger.scriptParsed", {
                    scriptId: "" + document.docId,
                    url: document.url,
                    startLine: 0,
                    startColumn: 0,
                    endLine: document.length,
                    endColumn: document.length,
                    isContentScript: false,
                    sourceMapURL: document.sourceMapUrlFromHeader
                });
            }
        }

        private onRemoveDocuments(docIds: number[]): void {
        }

        private onUpdateDocuments(documents: IDocument[]): void {
        }

        private onResolveBreakpoints(breakpoints: IResolvedBreakpointInfo[]): void {
        }

        private onBreak(breakEventInfo: IBreakEventInfo): boolean {
            this._isAtBreakpoint = true;

            var callFrames = [];
            var frames = this._debugger.getFrames(0);
            for (var i = 0; i < frames.length; i++) {
                var scopes = [];

                var localId = this._debugger.getLocals(frames[i].callFrameId);
                if (localId) {
                    scopes.push({
                        object: {
                            className: "Object",
                            description: "Object",
                            objectId: "" + localId,
                            type: "object"
                        },
                        type: "local"
                    });

                    var locals = this._debugger.getChildProperties(localId, 0, 0);
                    for (var j = 0; j < locals.propInfos.length; j++) {
                        var prop = locals.propInfos[j];
                        if (prop.fake) {
                            scopes.push({
                                object: {
                                    className: "Object",
                                    description: "Object",
                                    objectId: "" + prop.propertyId,
                                    type: "object"
                                },
                                type: "closure"
                            });
                        }
                    }
                }

                callFrames.push({
                    callFrameId: "" + frames[i].callFrameId,
                    functionName: frames[i].functionName,
                    location: this.getLocationFromOffset(frames[i].location.docId, frames[i].location.start),
                    scopeChain: scopes,
                    this: null
                });
            }

            this.postNotification("Debugger.paused", {
                callFrames: callFrames,
                reason: "other",
                data: null
            });
            return true;
        }
    }

    export class App {
        private _proxy: DebuggerProxy;

        public main(): void {
            this._proxy = new DebuggerProxy();
        }
    }

    var app = new App();
    app.main();
}
