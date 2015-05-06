//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="Interfaces.d.ts"/>

module Proxy {
    "use strict";
    export class Common {
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