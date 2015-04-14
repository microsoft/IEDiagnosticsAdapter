//
// Copyright (C) Microsoft. All rights reserved.
//

declare module DiagnosticsOM {
    export interface IExternalMessageEvent extends Event {
        type: string;
        message: any;
    }

    export interface IExternal {
        allowProcessToTakeForeground(): void;
        sendMessage(...args: string[]): void;
        takeForeground(): void;
    }

    export interface IEventDispatcher {
        addEventListener(eventType: string, callback: Function): void;
        removeEventListener(eventType: string, callback: Function): void;
    }

    export interface IBrowserBeforeNavigateEvent {
        /*get*/ browserOrWindow: any; // Browser or Window
        /*get*/ flags: number;
        /*get*/ targetFrameName?: string;
        /*get*/ url: string;
    }

    export interface IBrowserNavigateCompleteEvent {
        /*get*/ browserOrWindow: any; // Browser or Window
        /*get*/ url: string;
    }

    export interface IBrowserDocumentCompleteEvent {
        /*get*/ browserOrWindow: any; // Browser or Window
        /*get*/ url: string;
    }

    export interface IBrowserNavigateErrorEvent {
        /*get*/ browserOrWindow: any; // Browser or Window
        /*get*/ statusCode: number;
        /*get*/ targetFrameName: string;
        /*get*/ url: string;
    }

    export interface IBrowserBeforeScriptExecuteEvent {
        /*get*/ window: Window;
    }

    export interface IBrowserConsoleMessageEvent {
        /*get*/ column?: number;
        /*get*/ level: number;
        /*get*/ line?: number;
        /*get*/ messageId: number;
        /*get*/ messageText: string;
        /*get*/ source?: string;
        /*get*/ url?: string;
        /*get*/ fileUrl?: string;
    }

    export interface IBrowserWebWorkerCreatedEvent {
        /*get*/ id: number;
        /*get*/ worker: IWebWorker;
    }

    export interface IWebWorker {
        /*get*/ id: number;

        executeScriptAsync(script: string, scriptName: string): void;
        postMessage(type: string, message: string): void;
        addEventListener(type: string, listener: (args: Event) => void): void;
    }

    export interface IBrowserWebWorkerClosedEvent {
        /*get*/ id: number;
    }

    export interface IBrowserInspectElementEvent {
        /*get*/ target: any;
    }

    export interface IBrowser extends IEventDispatcher {
        /*get*/ browserMode: string;
        /*get*/ defaultDocumentMode: number;
        /*get*/ document: HTMLDocument;
        /*get*/ documentMode: number;
        elementSelectionEventsEnabled: boolean;
        forceEdgeModeDocumentFamily: boolean;
        workerStartupScript: string;
        /*get*/ workers: IWebWorkers;

        createSafeFunction(targetOM: any, func: Function): Function;
        executeScript(code: string, targetFrame?: any): any;
        getUniqueID? (element: Element): string;
        highlightElement(elementOrNull: Element, marginColor: string, borderColor: string, paddingColor: string, contentColor: string): void;
        refresh(): void;
        takeVisualSnapshot(width?: number, height?: number, keepAspectRatio?: boolean): Blob;
        enumerateStyleSheets(): void;
    }

    export interface IWebWorkers {
        [index: number]: IWebWorker;
    }

    export interface IElementEventListener {
        /*get*/ column: number;
        /*get*/ cookie: number;
        /*get*/ document: string;
        /*get*/ eventName: string;
        /*get*/ functionName: string;
        /*get*/ line: number;
        /*get*/ usesCapture: boolean;
    }

    export interface IElementEventHelper extends IEventDispatcher {
        getEventHandlers(): any;
    }

    export interface IDom {
        getCrossSiteWindow(currentWindow: Window, targetWindow: Window): Window;
        getElementByUniqueId(id: string): Element;
        getElementEventHelper(element: Element): IElementEventHelper;
        getElementInnerHTML(element: Element): string;
        getElementOuterHTML(element: Element): string;
    }

    export interface IStyleProperty {
        /*get*/ activeInBlock: boolean;
        enabled: boolean;
        important: boolean;
        /*get*/ longhands: IStylePropertyList;
        /*get*/ propertyName: string;
        /*get*/ status: string;
        value: string;
    }

    export interface IStylePropertyList {
        /*get*/ length: number;
        [index: number]: IStyleProperty;
    }

    export interface ISourceLocation {
        /*get*/ line: number;
        /*get*/ column: number;
        /*get*/ fileURI: string;
    }

    export interface ITracedStyles {
        getAppliedProperties(): string[];
        getAppliedStyles(filter?: string): CSSStyleDeclaration[];
        getInheritanceChain(filter?: string): HTMLElement[];
        isInheritable(property: string): boolean;
    }

    export interface IStyles {
        calculateTracedStyles(element?: HTMLElement): void;
        getDynamicPropertyList(style: CSSStyleDeclaration): IStylePropertyList;
        getParsedPropertyList(style: CSSStyleDeclaration): IStylePropertyList;
        getSourceLocation(style: CSSStyleDeclaration): ISourceLocation;
        getSpecificity(selector: string): any;
        getTracedStyles(element: Element): ITracedStyles;
    }

    export interface IResources {
        clearAllUrlRedirects(): void;
        clearAppCache(): void;
        clearBrowserCache(): void;
        clearBrowserCacheForDomain(): void;
        clearUrlRedirect(sourceUrl: string): void;
        setUrlRedirect(sourceUrl: string, replacementUrl: string): void;

        alwaysRefreshFromServer: boolean;
        /*get*/ cookies: ICookies;
        /*get*/ indexedDB: IIndexedDb;
        /*get*/ localStorage: IStorage;
        /*get*/ memory: IMemoryProfiler;
        /*get*/ sessionStorage: IStorage;
    }

    export interface ICookieEntry {
        domain: string;
        expires: Date;
        httpOnly: boolean;
        name: string;
        path: string;
        secure: boolean;
        session: boolean;
        value: string;
    }

    export interface ICookies {
        cookieCacheSize: number;

        createCookie(cookieName: string): ICookieEntry;
        deleteCookie(cookieName: string, domain: string): void;
        disableAll(): void;
        getAllCookies(): ICookieEntry[];
        setCookie(valueCookie: ICookieEntry): void;
    }

    export interface IStorage {
        quotaLimiting: boolean;
        /*get*/ size: number;
    }

    export interface IIndexedDb {
        quotaLimiting: boolean;
        /*get*/ size: number;

        clearIndexedDB(): void;
    }

    export interface IMemoryProfiler {
        /*get*/ memoryProfileHeapSize: number;
        /*get*/ processPointerSize: number;
        /*get*/ processPrivateBytes: number;

        getMemoryProfile(): Blob;
        getMemoryProfileFromWorkerURN(): Blob;
        triggerGarbageCollection(): void;
    }

    export interface IEmulation {
        /*get*/ geoLocation: IGeoLocation;
        /*get*/ quirksEmulation: boolean;
        userAgentString: string;
        /*get*/ viewport: IViewport;

        getActiveStyling(element: Node): boolean;
        getCompatUserAgentStrings(): string[];
        getFocusStyling(element: Node): boolean;
        getHoverStyling(element: Node): boolean;
        getLinkStyling(element: Node): boolean;
        getVisitedStyling(element: Node): boolean;
        setActiveStyling(element: Node, active: boolean): void;
        setFocusStyling(element: Node, focus: boolean): void;
        setHoverStyling(element: Node, hover: boolean): void;
        setLinkStyling(element: Node, link: boolean): void;
        setVisitedStyling(element: Node, visited: boolean): void;
    }

    export interface IViewport {
        heightInPx: number;
        orientation: string;
        widthInPx: number;

        restoreOriginalSettings(): void;
        setDimensions(widthInPx: number, heightInPx: number, diagonalInInches: number): void;
    }

    export interface IGeoLocation {
        accuracy: number;
        altitude: number;
        altitudeAccuracy: number;
        heading: number;
        latitude: number;
        longitude: number;
        speed: number;

        clearAccuracy(): void;
        clearAll(): void;
        clearAltitudeAccuracy(): void;
        clearAltitude(): void;
        clearHeading(): void;
        clearLatitude(): void;
        clearLongitude(): void;
        clearSpeed(): void;
    }

    export interface ITimerCallback {
        (): void;
    }

    export interface ITimers {
        clearInterval(callback: ITimerCallback): void;
        clearTimeout(callback: ITimerCallback): void;
        setInterval(callback: ITimerCallback, timeout: number): void;
        setTimeout(callback: ITimerCallback, timeout: number): void;
    }

    export interface IGlobalScope extends ITimers {
        /*get*/ browser: DiagnosticsOM.IBrowser;
        /*get*/ diagnostics: DiagnosticsOM.IGlobalScope;
        /*get*/ dom: DiagnosticsOM.IDom;
        /*get*/ emulation: DiagnosticsOM.IEmulation;
        /*get*/ external: DiagnosticsOM.IExternal;
        /*get*/ resources: DiagnosticsOM.IResources;
        /*get*/ styles: DiagnosticsOM.IStyles;
    }

    export interface ISourceEdit {
        newDocId?: number;
        error?: {
            message: string;
            line: number;
            column: number;
        }
    }

    export interface IFrame {
        /*get*/ column: number;
        /*get*/ documentID: number;
        /*get*/ documentUrl: string;
        /*get*/ functionName: string;
        /*get*/ line: number;
    }
}

declare var browser: DiagnosticsOM.IBrowser;
declare var diagnostics: DiagnosticsOM.IGlobalScope;
declare var dom: DiagnosticsOM.IDom;
declare var emulation: DiagnosticsOM.IEmulation;
// NOTE: cannot redefine `external` as it is defined in lib.d.ts.
// declare var external: DiagnosticsOM.IExternal;
declare var resources: DiagnosticsOM.IResources;
declare var styles: DiagnosticsOM.IStyles;