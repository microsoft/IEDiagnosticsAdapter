//
// Copyright (C) Microsoft. All rights reserved.
//

declare module Proxy {
    enum NodeType {
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
} 