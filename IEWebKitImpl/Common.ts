//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="Interfaces.d.ts"/>

module IEDiagnosticsAdapter.Common {
    "use strict";

    var mapDocumentToFrameId: WeakMap<Document, string> = new WeakMap<Document, string>();
    var nextAvailableFrameid: number = 1500.1;

    // The string below is a base64 encoded image of a red x on a white background.
    // It's used whenever the decoding of the blob fails, typically for screencasts.
    var brokenImg: string = "/9j/4AAQSkZJRgABAQEAYABgAAD/4QBmRXhpZgAATU0AKgAAAAgABAEaAAUAAAABAAAAPgEbAAUAAAABAAAARgEoAAMAAAABAAIAAAExAAIAAAAQAAAATgAAAAAAAABgAAAAAQAAAGAAAAABcGFpbnQubmV0IDQuMC41AP/bAEMABAIDAwMCBAMDAwQEBAQFCQYFBQUFCwgIBgkNCw0NDQsMDA4QFBEODxMPDAwSGBITFRYXFxcOERkbGRYaFBYXFv / bAEMBBAQEBQUFCgYGChYPDA8WFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFv / AABEIAMgAyAMBIgACEQEDEQH / xAAfAAABBQEBAQEBAQAAAAAAAAAAAQIDBAUGBwgJCgv / xAC1EAACAQMDAgQDBQUEBAAAAX0BAgMABBEFEiExQQYTUWEHInEUMoGRoQgjQrHBFVLR8CQzYnKCCQoWFxgZGiUmJygpKjQ1Njc4OTpDREVGR0hJSlNUVVZXWFlaY2RlZmdoaWpzdHV2d3h5eoOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4eLj5OXm5 + jp6vHy8 / T19vf4 + fr / xAAfAQADAQEBAQEBAQEBAAAAAAAAAQIDBAUGBwgJCgv / xAC1EQACAQIEBAMEBwUEBAABAncAAQIDEQQFITEGEkFRB2FxEyIygQgUQpGhscEJIzNS8BVictEKFiQ04SXxFxgZGiYnKCkqNTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqCg4SFhoeIiYqSk5SVlpeYmZqio6Slpqeoqaqys7S1tre4ubrCw8TFxsfIycrS09TV1tfY2dri4 + Tl5ufo6ery8 / T19vf4 + fr / 2gAMAwEAAhEDEQA/APGaKK3438V / Dj4gbl + 1aLr+i3H0eJx + jKQfdWU9wa / M4q + r2P74q1OW8YWc2m0m7Xtbybtdq7s7X2MCiv0O / Zf + NWk / FXw95E / lWXiOyjBvrEHiQdPOizyUJ6jqpOD2J9Wr6CjkUK0FOFa6fl / wT8VzTxgxeV4ueExeWctSO69r + K / d6p9H1Pybor9ZKK1 / 1c/6e/h / wTg / 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";

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
    export function getValidWindow(context: Window, obj: any): IgetValidWindowResponse {
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

    export function convertBlobToBase64(blob: Blob, callback: IConvertBlobToBase64Callback): void {
        try {
            if (blob) {
                var reader = new FileReader();
                reader.readAsText(blob);
                reader.onloadend = function (): void {
                    var base64data = reader.result;
                    callback(base64data);
                };
            }
        } catch (ex) {
            var errorResponse = {
                partId: -1,
                data: ex.message
            };
            callback(brokenImg);
        }
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