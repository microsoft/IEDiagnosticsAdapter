//
// Copyright (C) Microsoft. All rights reserved.
//

declare module F12.Proxy {
    enum NodeType {
        ELEMENT_NODE = 1,
        ATTRIBUTE_NODE = 2,
        TEXT_NODE = 3,
        CDATA_SECTION_NODE = 4,
        ENTITY_REFERENCE_NODE = 5,
        ENTITY_NODE = 6,
        PROCESSING_INSTRUCTION_NODE = 7,
        COMMENT_NODE = 8,
        DOCUMENT_NODE = 9,
        DOCUMENT_TYPE_NODE = 10,
        DOCUMENT_FRAGMENT_NODE = 11,
        NOTATION_NODE = 12,
    }

    //// dom
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
        xmlVersion?: string;
    }


    //// other
    interface messageResponce {
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
        params: any;
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
        value: any;
    }

    interface IBrowser {
        addEventListener(eventType: string, callback: Function): void;
        removeEventListener(eventType: string, callback: Function): void;
        /*get*/ browserMode: string;
        /*get*/ defaultDocumentMode: number;
        /*get*/ document: HTMLDocument;
        /*get*/ documentMode: number;
        elementSelectionEventsEnabled: boolean;
        forceEdgeModeDocumentFamily: boolean;
        workerStartupScript: string;

        createSafeFunction(targetOM: any, func: Function): Function;
        executeScript(code: string, targetFrame?: any): any;
        highlightElement(elementOrNull: Element, marginColor: string, borderColor: string, paddingColor: string, contentColor: string): void;
        refresh(): void;
        takeVisualSnapshot(width?: number, height?: number, keepAspectRatio?: boolean): Blob;
        enumerateStyleSheets(): void;
    }
} 