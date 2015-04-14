//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="IE11.DiagnosticOM.d.ts" />
/// <reference path="Interfaces.d.ts"/>
/// <reference path="Browser.ts"/>


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
            // Calling toString on the expires for a session cookie will result in unspecified error
            if (msCookie && msCookie.session) {
                this.expires = "";
            } else {
                this.expires = msCookie.expires && msCookie.expires.toString() || "unknown";
            }

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

    export class PageHandler implements IDomainHandler {
        constructor() {
        }

        public processMessage(method: string, request: IWebKitRequest): void {
            var processedResult: IWebKitResponse;

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

                case "getResourceTree":
                    processedResult = this.getResourceTree(request);
                    break;

                default:
                    processedResult = null;
                    break;
            }

            browserHandler.postResponse(request.id, processedResult);
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
                        cookies: webkitCookies
                    }
                };
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }

        private deleteCookie(request: IWebKitRequest): IWebKitResponse {
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

        private getResourceTree(request: IWebKitRequest): IWebKitResponse {
            var processedResult: any = {};

            try {
                var url = browser.document.parentWindow.location.href;
                var mimeType = "text/html";
                // Casting to any as the default lib.d.ts does not have it on the Location object
                var securityOrigin = (<any>browser.document.parentWindow.location).origin;

                processedResult = {
                    result: {
                        frameTree: {
                            frame: {
                                id: "1500.1",
                                loaderId: "1500.2",
                                url: url,
                                mimeType: mimeType,
                                securityOrigin: securityOrigin
                            },
                            resources: []
                        }
                    }
                };
            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }
    }

    export var pageHandler: PageHandler = new PageHandler();
}