//
// Copyright (C) Microsoft. All rights reserved.
//

// this is because we want ot use maps but not es6
interface Map<K, V> {
    clear(): void;
    delete(key: K): boolean;
    forEach(callbackfn: (value: V, index: K, map: Map<K, V>) => void, thisArg?: any): void;
    get(key: K): V;
    has(key: K): boolean;
    set(key: K, value: V): Map<K, V>;
    size: number;
}
declare var Map: {
    new <K, V>(): Map<K, V>;
}

interface WeakMap<K, V> {
    clear(): void;
    delete(key: K): boolean;
    get(key: K): V;
    has(key: K): boolean;
    set(key: K, value: V): WeakMap<K, V>;
}
declare var WeakMap: {
    new <K, V>(): WeakMap<K, V>;
}

interface Set<T> {
    add(value: T): Set<T>;
    clear(): void;
    delete(value: T): boolean;
    forEach(callbackfn: (value: T, index: T, set: Set<T>) => void, thisArg?: any): void;
    has(value: T): boolean;
    size: number;
}
declare var Set: {
    new <T>(): Set<T>;
}

declare module IEDiagnosticsAdapter {
    const enum NodeType {
        ElementNode = 1,
        AttributeNode = 2,
        TextNode = 3,
        CdataSectionNode = 4,
        EntityReferenceNode = 5,
        EntityNode = 6,
        ProcessingInstructionNode = 7,
        CommentNode = 8,
        DocumentNode = 9,
        DocumentTypeNode = 10,
        DocumentFragmentNode = 11,
        NotationNode = 12
    }

    //// DOM
    export interface INode {
        attributes?: string[];
        childNodeCount?: number;
        children?: INode[];
        contentDocument?: INode;
        documentURL?: string;
        frameId?: string;
        internalSubnet?: string;
        localName: string;
        name?: string;
        nodeId: number;
        nodeName: string;
        nodeType: number;
        nodeValue: string;
        publicId?: string;
        systemId?: string;
        value?: string;
        baseUrl?: string;
        xmlVersion?: string;
    }

    // WebKit Connection
    interface IMessageResponce {
        id?: number;
        error?: string;
        result?: string;
    }
    interface IWebKitRequest {
        id: number;
        method: string;
        params: any;
    }

    interface IWebKitResponse {
        id: number;
        error?: any;
        result?: any;
    }

    interface IWebKitNotification {
        method: string;
        params?: any;
    }

    interface IWebKitResult {
        error?: any;
        result?: any;
    }

    interface IWebKitRemoteObject {
        className?: string;
        description: string;
        objectId: string;
        subType?: string;
        type: string;
        value?: any;
    }

    interface IWebKitCookie {
        name: string;
        value: string;
        domain: string;
        path: string;
        expires: string;
        size: number;
        httpOnly: boolean;
        secure: boolean;
        session: boolean;
    }

    interface IRange {
        endColumn: number;
        endLine: number;
        startColumn: number;
        startLine: number;
    }

    interface ILineCol {
        line: number;
        column: number;
    }

    export interface ICssDeclaration {
        originalOffset: number;
        endOffset?: number; // todo: Ensure that everything has endOffset once parsing is finished
        property: string;
        value: string;
        isMissingSemicolon?: boolean;
        isDisabled?: boolean;
        disabledFullText?: string;
    }

    export interface ICssRuleset {
        originalOffset: number;
        endOffset?: number; // todo: Ensure that everything has endOffset once parsing is finished
        selector: string;
        declarations: ICssDeclaration[];
    }

    export interface ICssMediaQuery {
        originalOffset: number;
        endOffset?: number; // todo: Ensure that everything has endOffset once parsing is finished
        query: string;
        rulesets: ICssRuleset[];
    }

    interface IWebKitScreencastFrame {
        data: string;
        metadata: IWebKitScreencastFrameMetadata;
        frameNumber: number;
    }

    interface IWebKitScreencastFrameMetadata {
        pageScaleFactor: number;
        offsetTop: number;
        deviceWidth: number;
        deviceHeight: number;
        scrollOffsetX: number;
        scrollOffsetY: number;
    }

    interface IWebKitScreencastFrameCallback {
        (frame: IWebKitScreencastFrame): any;
    }

    interface IWebKitRecordedFrame {
        data: string;
        timestamp: Date;
    }

    interface IWebKitRecordedFrameCallback {
        (frame: IWebKitRecordedFrame): any;
    }

    interface IConvertBlobToBase64Callback {
        (base64: string): any;   
    }

    interface IDomainHandler {
        processMessage(method: string, request: IWebKitRequest): void;
    }

    interface IgetValidWindowResponse {
        isValid: boolean;
        window: Window;
    }

    interface IProxyDispatch {
        alert(message: string): void;
        postMessage(message: string): void;
        addEventListener(type: string, listener: Function): void;
        addEventListener(type: "onmessage", listener: (data: string) => void): void;
    }

    interface IProxyDebuggerDispatch extends IProxyDispatch {
        postMessageToEngine(id: string, isAtBreakpoint: boolean, data: string): void;
    }
} 