//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="Interfaces.d.ts"/>

module Proxy.Common {
    "use strict";

    var mapDocumentToFrameId: WeakMap<Document, string> = new WeakMap<Document, string>();
    var nextAvailableFrameid: number = 1500.1;

    /* Gets the Id of the iframe where doc lives.
     * @param doc The document to that lives in the iframe that gets returned
     * @return frameId is the Id of the iframe where doc lives.
     */
    export function getiframeId(doc: Document): string {
        if (!doc || doc.nodeType !== NodeType.DocumentNode) {
            throw new Error("invalid node");
        }

        if (mapDocumentToFrameId.has(doc)) {
            return mapDocumentToFrameId.get(doc);
        }

        var frameId: number = nextAvailableFrameid;
        nextAvailableFrameid = nextAvailableFrameid + 100;
        mapDocumentToFrameId.set(doc, "" + frameId);
        return "" + frameId;
    }

    /* Safely validates the window and gets the valid cross-site window when appropriate.
     * @param context The window to use as the context for the call to getCrossSiteWindow if necessary.
     * @param obj The object to attempt to get a valid window out of.
     * @return .isValid is true if obj is a valid window and .window is obj or the cross-site window if necessary.
     */
    export function getValidWindow(context: Window, obj: any): getValidWindowResponse {
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

    /* Gets the Window containing the document.
     * @param doc The document that we want a Window from
     * @return Window The default view for doc.
     */
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

    /* Creates a response to send back to the Chrome Dev Tools
     * @param id The request id we are responding to
     * @param value The IWebKitResult containing the bulk of the response
     * @return response The response to send to the Chrome Dev Tools.
     */
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