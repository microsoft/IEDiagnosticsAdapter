//
// Copyright (C) Microsoft. All rights reserved.
//

declare module F12.Proxy {
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