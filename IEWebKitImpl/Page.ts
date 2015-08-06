//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="IE11.DiagnosticOM.d.ts" />
/// <reference path="Interfaces.d.ts"/>
/// <reference path="Browser.ts"/>

/// Proxy to handle the page domain of the Chrome remote debug protocol 
module IEDiagnosticsAdapter {
    export class WebkitCookie implements IWebKitCookie {
        public name: string;
        public value: string;
        public domain: string;
        public path: string;
        public expires: string;
        public size: number;
        public httpOnly: boolean;
        public secure: boolean;
        public session: boolean;

        constructor(msCookie?: DiagnosticsOM.ICookieEntry) {
            this.name = msCookie && msCookie.name || "unknown";
            this.value = msCookie && msCookie.value || "unknown";
            this.domain = msCookie && msCookie.domain || "unknown";
            this.path = msCookie && msCookie.path || "unknown";
            // Calling toString on the expires for a session cookie will result in unspecified error
            if (msCookie && msCookie.session) {
                this.expires = "";
            } else {
                this.expires = msCookie.expires && msCookie.expires.toString() || "unknown";
            }

            if (msCookie) {
                // Rough approximation of size as IE doesn't report the actual size
                this.size = msCookie.name.length + msCookie.value.length;
            } else {
                this.size = 0;
            }

            this.httpOnly = msCookie && msCookie.httpOnly || false;
            this.secure = msCookie && msCookie.secure || false;
            this.session = msCookie && msCookie.session || false;
        }
    }

    export class WebkitScreencastFrameMetadata implements IWebKitScreencastFrameMetadata {
        public pageScaleFactor: number;
        public offsetTop: number;
        public deviceWidth: number;
        public deviceHeight: number;
        public scrollOffsetX: number;
        public scrollOffsetY: number;

        constructor(pageScaleFactor?: number, offsetTop?: number, deviceWidth?: number, deviceHeight?: number, scrollOffsetX?: number, scrollOffsetY?: number) {
            this.pageScaleFactor = pageScaleFactor || 1;
            this.offsetTop = offsetTop || 0;
            this.deviceWidth = deviceWidth || 1;
            this.deviceHeight = deviceHeight || 1;
            this.scrollOffsetX = scrollOffsetX || 0;
            this.scrollOffsetY = scrollOffsetX || 0;
        }
    }

    export class WebkitScreencastFrame implements IWebKitScreencastFrame {
        public data: string;
        public metadata: IWebKitScreencastFrameMetadata;
        public frameNumber: number;

        constructor(frameNumber?: number, data?: string, pageScaleFactor?: number, offsetTop?: number, deviceWidth?: number, deviceHeight?: number, scrollOffsetX?: number, scrollOffsetY?: number) {
            this.frameNumber = frameNumber;
            this.data = data;
            this.metadata = new WebkitScreencastFrameMetadata(pageScaleFactor, offsetTop, deviceWidth, deviceHeight, scrollOffsetX, scrollOffsetY);
        }

        public static captureFrame(frameNumber: number, callback: IWebKitScreencastFrameCallback): void {
            var window = browser.document.defaultView;
            var pageScaleFactor = window.devicePixelRatio;
            var offsetTop: number = window.document.body.offsetTop;
            var deviceWidth: number = (window.innerWidth > 0) ? window.innerWidth : screen.width;
            var deviceHeight: number = (window.innerHeight > 0) ? window.innerHeight : screen.height;
            var scrollOffsetX: number = window.pageXOffset;
            var scrollOffsetY: number = window.pageYOffset;
            var blobData: Blob = browser.takeVisualSnapshot(deviceWidth, deviceHeight, true);
            Common.convertBlobToBase64(blobData, (base64Data: string) => {
                var frame: WebkitScreencastFrame = new WebkitScreencastFrame(frameNumber, base64Data, pageScaleFactor, offsetTop, deviceWidth, deviceHeight, scrollOffsetX, scrollOffsetY);
                callback(frame);
            });
        }
    }

    export class WebkitRecordedFrame implements IWebKitRecordedFrame {
        public data: string;
        public timestamp: Date;

        constructor(data?: string, timestamp?: Date) {
            this.data = data;
            this.timestamp = timestamp;
        }

        public static captureFrame(callback: IWebKitRecordedFrameCallback): void {
            var window = browser.document.defaultView;
            var deviceWidth: number = (window.innerWidth > 0) ? window.innerWidth : screen.width;
            var deviceHeight: number = (window.innerHeight > 0) ? window.innerHeight : screen.height;
            var blobData: Blob = browser.takeVisualSnapshot(deviceWidth, deviceHeight, true);
            var timestamp = new Date();
            Common.convertBlobToBase64(blobData, (base64Data: string) => {
                var frame: WebkitRecordedFrame = new WebkitRecordedFrame(base64Data, timestamp);
                callback(frame);
            });
        }
    }

    export class ScreencastSession {
        private _frameId: number;
        private _framesAcked: boolean[];
        private _frameInterval: number = 250; // 60 fps is 16ms
        private _format: string;
        private _quality: number;
        private _maxWidth: number;
        private _maxHeight: number;

        constructor(format?: string, quality?: number, maxWidth?: number, maxHeight?: number) {
            this._format = format || "jpg";
            this._quality = quality || 100;
            this._maxHeight = maxHeight || 1024;
            this._maxWidth = maxWidth || 1024;
        }

        public dispose(): void {
            this.stop();
        }

        public start(): void {
            this._framesAcked = new Array();
            this._frameId = 1; // CDT seems to be 1 based and won't ack when 0

            setInterval(() => this.recordingLoop(), this._frameInterval);
        }

        public stop(): void {
            (<any>clearInterval)(() => this.recordingLoop); // The diagnosticOM clearInterval doesn't take an ID it just takes the function pointer
        }

        public ackFrame(frameNumber: number): void {
            this._framesAcked[frameNumber] = true;
        }

        private recordingLoop(): void {
            WebkitScreencastFrame.captureFrame(this._frameId, function (frame: WebkitScreencastFrame): void {
                browserHandler.postNotification("Page.screencastFrame", frame);
            });
            this._frameId++;
        }
    }

    export class FrameRecorder {
        private _frameBuffer: IWebKitRecordedFrame[];
        private _frameInterval: number = 64; // 60 fps is 16ms
        private _capturedFrames: number;
        private _maxFrameCount: number;

        constructor() {
        }

        public start(maxFrameCount?: number): void {
            this._maxFrameCount = maxFrameCount || 1000;
            this._frameBuffer = new Array();
            this._capturedFrames = 0;
            setInterval(this.recordingLoop, this._frameInterval);
        }

        public stop(): IWebKitRecordedFrame[] {
            (<any>clearInterval)(this.recordingLoop); // The diagnosticOM clearInterval doesn't take an ID it just takes the function pointer
            return this._frameBuffer;
        }

        private recordingLoop(): void {
            WebkitRecordedFrame.captureFrame((frame: WebkitRecordedFrame) => function (frame: WebkitRecordedFrame): void {
                // Assumes the frames are coming back in order... (even though they are async)
                if (this.capturedFrames < this.maxFrameCount) {
                    // Todo: Figure out if the implementation is meant to be a FIFO bufffer
                    this.frameBuffer.push(frame);
                    this.capturedFrames++;
                }
            });
        }
    }

    export class PageHandler implements IDomainHandler {
        private _screencastSession: ScreencastSession;
        private _frameRecorder: FrameRecorder;
        private _nextContextID: number;
        private _firstValidContextID: number;
        private _validContextIDs: Set<number>;

        constructor() {
            this._frameRecorder = new FrameRecorder();
            this._nextContextID = 1;
            this._firstValidContextID = 1;
        }

        public processMessage(method: string, request: IWebKitRequest): void {
            var processedResult: IWebKitResult;

            switch (method) {
                case "enable":
                    processedResult = { result: {} };
                    break;

                case "navigate":
                    processedResult = this.navigate(request);
                    break;

                case "getCookies":
                    processedResult = this.getCookies();
                    break;

                case "deleteCookie":
                    processedResult = this.deleteCookie(request);
                    break;

                case "getResourceTree":
                    processedResult = this.getResourceTree(request);
                    break;

                case "setOverlayMessage":
                    processedResult = {};
                    break;

                case "canScreencast":
                    processedResult = {
                        result: {
                            result: true
                        }
                    };
                    break;

                case "screencastFrameAck":
                    processedResult = this.screencastFrameAck(request);
                    break;

                case "startRecordingFrames":
                    processedResult = this.startRecordingFrames(request);
                    break;

                case "stopRecordingFrames":
                    processedResult = this.stopRecordingFrames(request);
                    break;

                case "startScreencast":
                    processedResult = this.startScreencast(request);
                    break;

                case "stopScreencast":
                    processedResult = this.stopScreencast();
                    break;

                case "canEmulate":
                    processedResult = { result: false };
                    break;

                case "setShowViewportSizeOnResize":
                    processedResult = { result: {} };
                    break;

                case "getAnimationsPlaybackRate":
                    processedResult = { result: { playbackRate: 1 } };
                    break;

                case "reload":
                    processedResult = this.reload();
                    break;

                case "getNavigationHistory":
                    // This looks like it is used for screen casting, but does not seem necessary to implement so leaving empty for now
                    processedResult = {};
                    break;

                default:
                    processedResult = null;
                    break;
            }

            browserHandler.postResponse(request.id, processedResult);
        }

        public getAllDocs(doc: Document): Document[] {
            var docs = [doc];

            var tags = doc.querySelectorAll("iframe, frame");
            for (var i = 0, n = tags.length; i < n; i++) {
                var frame = <HTMLIFrameElement>tags[i];
                var view = Common.getDefaultView(doc);
                var result: IgetValidWindowResponse = Common.getValidWindow(view, frame.contentWindow);
                if (result.isValid) {
                    docs.concat(this.getAllDocs(result.window.document));
                }
            }

            return docs;
        }

        public onNavigate(): void {
            var docs = this.getAllDocs(browser.document);

            docs.forEach(doc => {
                browserHandler.postNotification("Page.frameStartedLoading", { FrameId: Common.getiframeId(doc) });
            });

            for (var i = this._firstValidContextID; i < this._nextContextID; i++) {
                (<any>external).sendMessage("postMessage", JSON.stringify({
                    method: "Runtime.executionContextDestroyed",
                    params: {
                        executionContextId: i
                    }
                }));
            }

            this._firstValidContextID = this._nextContextID + 1;

            docs.forEach(doc => {
                this.createExecutionContext(Common.getiframeId(doc));
            });

            docs.forEach(doc => {
                var securityOrigin = (<any>doc.defaultView.location).origin || "";
                var frameId = Common.getiframeId(doc);
                var frameNavigatedParams = {
                    frame: {
                        id: frameId,
                        loaderId: this.getLoaderID(frameId),
                        url: doc.defaultView.location.href,
                        mimeType: "text/html", // todo: doc.mimetype is "HTM File", if documents ever have a different mimeType figure out how to get it dynamically
                        securityOrigin: securityOrigin
                    }
                };

                browserHandler.postNotification("Page.frameNavigated", frameNavigatedParams);
            });

            browserHandler.postNotification("Page.domContentEventFired", { timestamp: 396680 }); // todo: use an accurate time stamp
            browserHandler.postNotification("Page.loadEventFired", { timestamp: 396680 }); // todo: use an accurate time stamp
            
            docs.forEach(doc => {
                browserHandler.postNotification("Page.frameStoppedLoading", { frameId: Common.getiframeId(doc) });
            });
        }

        private reload(): IWebKitResult {
            var processedResult: IWebKitResult = {};

            try {
                browser.refresh();
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }

        private startScreencast(request: IWebKitRequest): IWebKitResult {
            var processedResult: IWebKitResult = {};

            try {
                var format: string = request.params.format;
                var quality: number = request.params.quality;
                var maxWidth: number = request.params.maxWidth;
                var maxHeight: number = request.params.maxHeight;

                if (this._screencastSession) {
                    // Session has already started so disposing of the last one.
                    this._screencastSession.dispose();
                } 

                this._screencastSession = new ScreencastSession(format, quality, maxWidth, maxHeight);
                this._screencastSession.start();
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }

        private stopScreencast(): IWebKitResult {
            var processedResult: IWebKitResult = {};

            try {
                if (!this._screencastSession) {
                    throw new Error("Screencast session not started.");
                }

                this._screencastSession.stop();
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }

        private screencastFrame(): IWebKitResult {
            var processedResult: IWebKitResult = {};

            try {
                if (!this._screencastSession) {
                    throw new Error("Screencast session not started.");
                }
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }

        private screencastFrameAck(request: IWebKitRequest): IWebKitResult {
            var processedResult: IWebKitResult = {};

            try {
                var frameNumber: number = request.params.frameNumber;

                this._screencastSession.ackFrame(frameNumber);
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }

        private startRecordingFrames(request: IWebKitRequest): IWebKitResult {
            var processedResult: IWebKitResult = {};

            try {
                var maxFrameCount: number = request.params.maxFrameCount;
                this._frameRecorder.start(maxFrameCount);
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }

        private stopRecordingFrames(request: IWebKitRequest): IWebKitResult {
            var processedResult: IWebKitResult = {};

            try {
                var frames: IWebKitRecordedFrame[] = this._frameRecorder.stop();

                processedResult = {
                    result: {
                        frames: frames
                    }
                };
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }

        private getCookies(): IWebKitResult {
            var processedResult: IWebKitResult = {};
            var webkitCookies: Array<WebkitCookie> = new Array<WebkitCookie>();

            try {
                var msCookies = resources.cookies.getAllCookies();
                for (var i = 0; msCookies && i < msCookies.length; i++) {
                    webkitCookies.push(new WebkitCookie(msCookies[i]));
                }

                processedResult = {
                    result: {
                        cookies: webkitCookies
                    }
                };
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }

        private deleteCookie(request: IWebKitRequest): IWebKitResult {
            var processedResult: IWebKitResult = {};
            var cookieName: string = request.params.cookieName;
            var url: string = request.params.url;

            try {
                resources.cookies.deleteCookie(cookieName, url);
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }

        private navigate(request: IWebKitRequest): IWebKitResult {
            var processedResult: IWebKitResult = {};

            try {
                if (request.params.url) {
                    browser.executeScript("window.location.href = '" + request.params.url + "'");

                    processedResult = {
                        result: {
                            frameId: 5000.1
                        }
                    };
                }
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }

        private createExecutionContext(frameId: string): void {
            var executionContextCreatedParams = {
                context: {
                    id: this._nextContextID++,
                    name: "", // I think that this field is only needed for Addons, which IE Does not support.
                    origin: "",
                    frameId: frameId
                }
            };

            browserHandler.postNotification("Runtime.executionContextCreated", executionContextCreatedParams);
        }

        // frame id is xxxx.1, loader id is xxxx.2
        private getLoaderID(frameId: string): string {
            return frameId.substring(0, frameId.length - 1) + "2";
        }

        private getResourceTreeRecursive(doc: Document, parentFrameId: string = ""): IWebKitResult {
            // Casting to any as the default lib.d.ts does not have it on the Location object    
            var securityOrigin = (<any>doc.defaultView.location).origin || "";
            var frameId = Common.getiframeId(doc);

            var loaderId: string = this.getLoaderID(frameId);
            var frameinfo: any = {
                frame: {
                    id: frameId,
                    loaderId: loaderId,
                    url: doc.defaultView.location.href,
                    mimeType: "text/html", // todo: doc.mimetype is "HTM File", if documents ever have a different mimeType figure out how to get it dynamically
                    securityOrigin: securityOrigin
                },
                resources: []
            };

            if (parentFrameId !== "") {
                frameinfo.frame.parentId = parentFrameId;
                frameinfo.frame.name = "frame";
            }

            // add in resources for this frame
            for (var i = 0; i < doc.scripts.length; i++) {
                Assert.areEqual(doc.scripts[i].tagName, "SCRIPT");
                var script: HTMLScriptElement = <HTMLScriptElement>doc.scripts[i];
                if (script.src) {
                    var mimeType = (mimeType !== "" ? script.type : "application/javascript");
                    frameinfo.resources.push({
                        url: script.src,
                        type: script.localName,
                        mimeType: mimeType
                    });
                }
            }

            for (var i = 0; i < doc.styleSheets.length; i++) {
                var styleSheet: StyleSheet = doc.styleSheets[i];
                if (styleSheet.href) {
                    frameinfo.resources.push({
                        url: styleSheet.href,
                        type: "Stylesheet",
                        mimeType: styleSheet.type
                        // todo: chrome sometimes adds a failed node here, figure out why/what it is used for
                    });
                }
            }

            // recursively add in childFrames
            var tags = doc.querySelectorAll("iframe, frame");
            if (tags.length > 0) {
                frameinfo.childFrames = [];
            }

            for (var i = 0, n = tags.length; i < n; i++) {
                var frame = <HTMLIFrameElement>tags[i];
                var view = Common.getDefaultView(doc);
                var result: IgetValidWindowResponse = Common.getValidWindow(view, frame.contentWindow);
                if (result.isValid) {
                    frameinfo.childFrames.push(this.getResourceTreeRecursive(result.window.document, frameId));
                }
            }

            // notify the Chrome dev tools that this frame is ready to receive console messages.
            this.createExecutionContext(frameId);
            return frameinfo;
        }

        private getResourceTree(request: IWebKitRequest): IWebKitResult {
            var processedResult: IWebKitResult = {};
            try {
                processedResult = {
                    result: {
                        frameTree: this.getResourceTreeRecursive(browser.document)
                    }
                };
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }
    }

    export var pageHandler: PageHandler = new PageHandler();
}