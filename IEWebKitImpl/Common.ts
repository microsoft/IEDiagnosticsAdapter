//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="Interfaces.d.ts"/>


module F12.Proxy {
    "use strict";
    export class Common {

        public static CreateResponse(id: number, value: IWebKitResult): IWebKitResponse {
            var response: IWebKitResponse = {
                id: id
            };

            if (value.error) {
                response.error = value.error;
            }

            if (value.result) {
                response.result = value.result;
            }
            return response;
        }
    }
    var common = new Common();
}