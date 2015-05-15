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
            // if (!doc || doc === browser.document) {
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
                    response.result = { }; // default response
                } else {
                    response.result = value.result;
                }          
            }

            return response;
        }
    }

    var common = new Common();
}