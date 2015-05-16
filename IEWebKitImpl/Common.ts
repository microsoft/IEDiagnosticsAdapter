//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="Interfaces.d.ts"/>

module Proxy {
    "use strict";
    export class Common {
        private static _mapDocumentToFrameID: WeakMap<Document, string> = new WeakMap<Document, string>();
        private static _nextAvailableFrameid: number = 1500.1;

        public static getiframeID(doc: Document): string {
            if (!doc || doc.nodeType !== NodeType.DocumentNode) {
                throw new Error("invalid node");
            }

            if (this._mapDocumentToFrameID.has(doc)) {
                return this._mapDocumentToFrameID.get(doc);
            }

            var frameID: number = this._nextAvailableFrameid;
            this._nextAvailableFrameid = this._nextAvailableFrameid + 100;
            this._mapDocumentToFrameID.set(doc, "" + frameID);
            return "" + frameID;
        }

        public static hasiframeID(doc: Document): boolean {
            return this._mapDocumentToFrameID.has(doc);
        }

        /* Safely validates the window and gets the valid cross-site window when appropriate.
         * @param context The window to use as the context for the call to getCrossSiteWindow if necessary.
         * @param obj The object to attempt to get a valid window out of.
         * @return .isValid is true if obj is a valid window and .window is obj or the cross-site window if necessary.
         */
        public static getValidWindow(context: Window, obj: any): { isValid: boolean; window: Window; } {
            try {
                if (Object.prototype.toString.call(obj) === "[object Window]") {
                    var w = obj;
                    if (this.isCrossSiteWindow(context, obj)) {
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

        private static isCrossSiteWindow(currentWindowContext: Window, obj: any): boolean {
            // it cannot be a cross site window if it's not a window
            try {
                var x = (<any>currentWindowContext).Object.getOwnPropertyNames(obj);
            } catch (e) {
                return true;
            }

            return false;
        }

        public static getDefaultView(doc: any): Window {
            if (doc) {
                if (typeof doc.defaultView !== "undefined") {
                    return doc.defaultView;
                } else {
                    return doc.parentWindow;
                }
            }

            return null;
        }

        public static createResponse(id: number, value: IWebKitResult): IWebKitResponse {
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
    }


    var common = new Common();
}