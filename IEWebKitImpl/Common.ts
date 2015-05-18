//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="Interfaces.d.ts"/>

module Proxy.Common {
    "use strict";

    var mapDocumentToFrameID: WeakMap<Document, string> = new WeakMap<Document, string>();
    var nextAvailableFrameid: number = 1500.1;

    export function getiframeID(doc: Document): string {
        if (!doc || doc.nodeType !== NodeType.DocumentNode) {
            throw new Error("invalid node");
        }

        if (mapDocumentToFrameID.has(doc)) {
            return mapDocumentToFrameID.get(doc);
        }

        var frameID: number = nextAvailableFrameid;
        nextAvailableFrameid = nextAvailableFrameid + 100;
        mapDocumentToFrameID.set(doc, "" + frameID);
        return "" + frameID;
    }

    export function hasiframeID(doc: Document): boolean {
        return mapDocumentToFrameID.has(doc);
    }

    /* Safely validates the window and gets the valid cross-site window when appropriate.
     * @param context The window to use as the context for the call to getCrossSiteWindow if necessary.
     * @param obj The object to attempt to get a valid window out of.
     * @return .isValid is true if obj is a valid window and .window is obj or the cross-site window if necessary.
     */
    export function getValidWindow(context: Window, obj: any): { isValid: boolean; window: Window; } {
        try {
            if (Object.prototype.toString.call(obj) === "[object Window]") {
                var w = obj;
                if (isCrossSiteWindow(context, obj)) {
                    w = dom.getCrossSiteWindow(context, obj);
                }

                if (w && w.document) {
                    return { isValid: true, window: w };
                }
            }
        } catch (e) {
            // iframes with non-html content as well as non-Window objects injected by usercode can throw.
            // Filter these out and do not consider them valid windows.
        }

        return { isValid: false, window: null };
    }

    export function getDefaultView(doc: any): Window {
        if (doc) {
            if (typeof doc.defaultView !== "undefined") {
                return doc.defaultView;
            } else {
                return doc.parentWindow;
            }
        }

        return null;
    }

    export function createResponse(id: number, value: IWebKitResult): IWebKitResponse {
        var response: IWebKitResponse = {
            id: id
        };

        if (!value) {
            response.error = new Error("No response specified");
        } else {
            if (value.error) {
                response.error = value.error;
            } else if (value.result === undefined || value.result === null) {
                response.result = {}; // default response
            } else {
                response.result = value.result;
            }
        }

        return response;
    }

    function isCrossSiteWindow(currentWindowContext: Window, obj: any): boolean {
        // it cannot be a cross site window if it's not a window
        try {
            var x = (<any>currentWindowContext).Object.getOwnPropertyNames(obj);
        } catch (e) {
            return true;
        }

        return false;
    }
}