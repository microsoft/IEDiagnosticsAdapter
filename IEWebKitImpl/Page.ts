//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="DiagnosticOM.d.ts" />
/// <reference path="Interfaces.d.ts"/>

/// Proxy to hande the page domain of the Chrome remote debug protocol 
module F12.Proxy {
    export class WebkitCookie implements IWebKitCookie {
        public name: string;
        public value: string;
        public domain: string;
        public path: string;
        public expires: string;
        public size: number;
        public httpOnly: boolean;
        public secure: boolean;
        public session: boolean;

        constructor(msCookie?: DiagnosticsOM.ICookieEntry) {
            this.name = msCookie && msCookie.name || "unknown";
            this.value = msCookie && msCookie.value || "unknown";
            this.domain = msCookie && msCookie.domain || "unknown";
            this.path = msCookie && msCookie.path || "unknown";
            this.expires = msCookie && msCookie.expires && msCookie.expires.toDateString() || "unknown";
            if (msCookie) {
                // Rough approximation of size as IE doesn't report the actual size
                this.size = msCookie.name.length + msCookie.value.length;
            } else {
                this.size = 0;
            }
            this.httpOnly = msCookie && msCookie.httpOnly || false;
            this.secure = msCookie && msCookie.secure || false;
            this.session = msCookie && msCookie.session || false;
        }
    }

    export class PageHandler implements IDomainHandler{
        constructor() {
        
        }

        private getCookies(): IWebKitResponse {
            var processedResult: any = {};
            var webkitCookies: Array<WebkitCookie> = new Array<WebkitCookie>();

            try {
                var msCookies = resources.cookies.getAllCookies();
                for (var i = 0; msCookies && i < msCookies.length; i++) {
                    webkitCookies.push(new WebkitCookie(msCookies[i]));
                }

                processedResult = {
                    result: {
                        coookies: webkitCookies
                    }
                };
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }
        
        private deleteCookie(request: IWebKitRequest):IWebKitResponse {
            var processedResult: any = {};
            var cookieName: string = request.params.cookieName;
            var url: string = request.params.url;

            try {
                resources.cookies.deleteCookie(cookieName, url);                
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }

        private navigate(request: IWebKitRequest): IWebKitResponse {
            var processedResult: any = {};
            
            try {
                if (request.params.url) {
                    browser.executeScript("window.location.href = '" + request.params.url + "'");

                    processedResult = {
                        result: {
                            frameId: 5000.1
                        }
                    };
                }
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }

        public processMessage(method: string, request: IWebKitRequest) {
            var processedResult;

            switch (method) {
                case "navigate":
                    processedResult = this.navigate(request);
                    break;

                case "getCookies":
                    processedResult = this.getCookies();
                    break;

                case "deleteCookie":
                    processedResult = this.deleteCookie(request);
                    break;

                default:
                    processedResult = {};
                    break;
            }

            browserHandler.PostResponse(request.id, processedResult);
        }
    }
    export var pageHandler: PageHandler = new PageHandler();
}