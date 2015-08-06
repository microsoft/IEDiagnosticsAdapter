//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="Interfaces.d.ts"/>

module IEDiagnosticsAdapter {
    "use strict";

    /**
     * Class that synchronously iterates through a CSS document and constructs a list of ICssMediaQuery and ICssRuleset objects.
     * The private members in this class store state used during the parse loop and that state is modified from
     * the various subroutines executed in the loop.
     */
    export class CssParser {
        // A list that contains any number of ICssMediaQuery and ICssRuleset objects
        private _rootNodes: any[];
        private _text: string;

        // Holds the index where text was last extracted from the source text
        private _lastCheckpoint: number;

        // Holds a string representing the current type of CSS construct that text is being extracted for
        private _state: CssToken;

        // Holds the index into the source text that the parser is currently inspecting at
        private _index: number;

        // Holds components of the CSS AST that are still being constructed
        private _currentRuleset: ICssRuleset;
        private _currentDeclaration: ICssDeclaration;
        private _currentMediaQuery: ICssMediaQuery;

        // Stores state about whether the loop has passed open quotes or open comments
        private _inComment: boolean;
        private _currentQuotationMark: string;
        private _nextCharIsEscaped: boolean;

        constructor(text: string) {
            this._rootNodes = [];
            this._text = text;

            // Statement control
            this._inComment = false;
            this._currentQuotationMark = "";
            this._nextCharIsEscaped = false;

            // Search maintenance state
            this._lastCheckpoint = 0;
            this._state = CssToken.Selector;

            // Storage for under-construction nodes
            this._currentRuleset = null;
            this._currentDeclaration = null;
            this._currentMediaQuery = null;
        }

        /** Returns an array containing ICssRuleset and ICssMediaQuery objects */
        public parseCss(): any[] {
            this.parseText();

            // Put any text that wasn't valid CSS into it's own node at the end of the file
            this.handleIncompleteBlocks();
            return this._rootNodes;
        }

        /** Returns an array containing a single rule, ICssRuleset and ICssMediaQuery objects */
        public parseInlineCss(): ICssRuleset {
            // inline CSS is just a list of properties. Set up the parser state to read them correctly.
            this._currentRuleset = { originalOffset: this._lastCheckpoint, selector: "DoesNotMatter", declarations: [] };
            this._state = CssToken.Property;

            this.parseText();

            Assert.isTrue(this._currentRuleset && this._rootNodes.length === 0, "Text was not valid inline CSS");
            this._currentRuleset.endOffset = this._text.length;

            return this._currentRuleset;
        }

        private parseText(): void {
            for (this._index = 0; this._index < this._text.length; this._index++) {
                if (this.handleQuoteCharacter()) {
                } else if (this.handleCommentCharacter()) {
                } else if (this.handleLeadingWhitespace()) {
                } else if (this.handleMediaQueryStart()) {
                } else if (this.handleMediaQueryOpenBracket()) {
                } else if (this.handleMediaQueryCloseBracket()) {
                } else if (this.handleSelectorOpenBracket()) {
                } else if (this.handlePropertyColon()) {
                } else if (this.handleValueSemicolon()) {
                } else if (this.handleSelectorCloseBracket()) {
                }
            }
        }

        private handleMediaQueryStart(): boolean {
            if (this._state === CssToken.Selector && !this._currentMediaQuery &&
                this._lastCheckpoint >= this._index &&
                this._text[this._index] === "@" && this._text.substr(this._index, 7).toLowerCase() === "@media ") {
                this._state = CssToken.Media;
                return true;
            }

            return false;
        }

        private handleMediaQueryOpenBracket(): boolean {
            if (this._state === CssToken.Media && this._text[this._index] === "{") {
                var mediaText = this._text.substring(this._lastCheckpoint, this._index);
                this._currentMediaQuery = { originalOffset: this._lastCheckpoint, query: mediaText, rulesets: [] };

                this._lastCheckpoint = this._index + 1;
                this._state = CssToken.Selector;

                return true;
            }

            return false;
        }

        private handleMediaQueryCloseBracket(): boolean {
            if (this._state === CssToken.Selector && this._text[this._index] === "}" && this._currentMediaQuery) {
                this._lastCheckpoint = this._index + 1;
                this._state = CssToken.Selector;
                this._currentMediaQuery.endOffset = this._index + 1;
                this._rootNodes.push(this._currentMediaQuery);
                this._currentMediaQuery = null;

                return true;
            }

            return false;
        }

        private handleSelectorOpenBracket(): boolean {
            if (this._state === CssToken.Selector && this._text[this._index] === "{") {
                var selectorText = this._text.substring(this._lastCheckpoint, this._index);
                this._currentRuleset = { originalOffset: this._lastCheckpoint, selector: selectorText, declarations: [] };

                this._lastCheckpoint = this._index + 1;
                this._state = CssToken.Property;

                return true;
            }

            return false;
        }

        private handlePropertyColon(): boolean {
            if (this._state === CssToken.Property && this._text[this._index] === ":") {
                var propertyText = this._text.substring(this._lastCheckpoint, this._index);
                this._currentDeclaration = { originalOffset: this._lastCheckpoint, property: propertyText, value: "" };

                this._lastCheckpoint = this._index + 1;
                this._state = CssToken.Value;

                return true;
            }

            return false;
        }

        private handleValueSemicolon(): boolean {
            if (this._state === CssToken.Value && this._text[this._index] === ";") {
                var valueText = this._text.substring(this._lastCheckpoint, this._index);
                this._currentDeclaration.value = valueText;
                this._currentDeclaration.endOffset = this._index + 1;
                this._currentRuleset.declarations.push(this._currentDeclaration);
                this._currentDeclaration = null;

                this._lastCheckpoint = this._index + 1;
                this._state = CssToken.Property;

                return true;
            }

            return false;
        }

        private handleSelectorCloseBracket(): boolean {
            if (this._text[this._index] === "}") {
                if (this._state === CssToken.Property) {
                    var incompleteDeclaration: ICssDeclaration = { originalOffset: this._lastCheckpoint, endOffset: this._index, property: this._text.substring(this._lastCheckpoint, this._index), value: null };
                    if (incompleteDeclaration.property.trim()) {
                        this._currentRuleset.declarations.push(incompleteDeclaration);
                    }

                    this._lastCheckpoint = this._index + 1;
                    this._state = CssToken.Selector;

                    if (this._currentMediaQuery) {
                        this._currentMediaQuery.endOffset = this._index;
                        this._currentMediaQuery.rulesets.push(this._currentRuleset);
                    } else {
                        this._currentRuleset.endOffset = this._index;
                        this._rootNodes.push(this._currentRuleset);
                    }

                    this._currentRuleset = null;
                    return true;
                }

                if (this._state === CssToken.Value) { // No closing semicolon, which is valid syntax
                    var valueText = this._text.substring(this._lastCheckpoint, this._index);
                    this._currentDeclaration.value = valueText;
                    this._currentDeclaration.isMissingSemicolon = true;
                    this._currentDeclaration.endOffset = this._index;
                    this._currentRuleset.declarations.push(this._currentDeclaration);
                    this._currentRuleset.endOffset = this._index;
                    this._currentDeclaration = null;

                    this._lastCheckpoint = this._index + 1;
                    this._state = CssToken.Selector;

                    if (this._currentMediaQuery) {
                        this._currentMediaQuery.rulesets.push(this._currentRuleset);
                    } else {
                        this._rootNodes.push(this._currentRuleset);
                    }

                    this._currentRuleset = null;
                    return true;
                }
            }

            return false;
        }

        private handleIncompleteBlocks(): void {
            if (this._currentMediaQuery) {
                this._lastCheckpoint = this._currentMediaQuery.originalOffset;
            } else if (this._currentRuleset) {
                this._lastCheckpoint = this._currentRuleset.originalOffset;
            }

            if (this._lastCheckpoint < this._text.length - 1) {
                var textNode: ICssRuleset = { selector: this._text.substr(this._lastCheckpoint), originalOffset: this._lastCheckpoint, declarations: null, endOffset: this._index + 1 };
                this._rootNodes.push(textNode);
            }
        }

        private handleCommentCharacter(): boolean {
            if (this._text.substr(this._index, 2) === "/*") {
                var endOfCommentIndex = this._text.indexOf("*/", this._index);
                if (endOfCommentIndex === -1) {
                    endOfCommentIndex = this._text.length;
                }

                if (this._state === CssToken.Property && !this._text.substring(this._lastCheckpoint, this._index).trim()) {
                    // this case is a disabled property
                    var colonIndex = this._text.indexOf(":", this._index);
                    if (colonIndex === -1 || colonIndex > endOfCommentIndex) {
                        Assert.fail("this is not a disabled property, hanlde this case later");
                    }

                    var propertyText = this._text.substring(this._index + 2, colonIndex);

                    var semiColonIndex = this._text.indexOf(";", this._index);
                    if (semiColonIndex === -1 || semiColonIndex >= endOfCommentIndex) {
                        var valueText = this._text.substring(colonIndex + 1, endOfCommentIndex);
                    } else {
                        var valueText = this._text.substring(colonIndex + 1, semiColonIndex);
                    }

                    this._currentDeclaration = { originalOffset: this._lastCheckpoint, property: propertyText, value: valueText };

                    this._currentDeclaration.isDisabled = true;
                    this._currentDeclaration.disabledFullText = this._text.substring(this._index, endOfCommentIndex + 2);

                    this._index = endOfCommentIndex + "*/".length - 1; // Adjust -1 because the loop will increment index by 1
                    this._currentDeclaration.endOffset = this._index + 1;

                    this._currentRuleset.declarations.push(this._currentDeclaration);
                    this._currentDeclaration = null;

                    this._lastCheckpoint = this._index + 1;
                } else {
                    // This case is for normal comments
                    this._index = endOfCommentIndex + "*/".length - 1; // Adjust -1 because the loop will increment index by 1
                }

                return true;
            }

            return false;
        }

        private handleQuoteCharacter(): boolean {
            if (this._currentQuotationMark) {
                if (this._nextCharIsEscaped) {
                    this._nextCharIsEscaped = false;
                } else if (this._text[this._index] === this._currentQuotationMark) {
                    this._currentQuotationMark = "";
                } else if (this._text[this._index] === "\\") {
                    this._nextCharIsEscaped = true;
                }

                return true;
            }

            if (this._text[this._index] === "\"" || this._text[this._index] === "'") {
                this._currentQuotationMark = this._text[this._index];
                return true;
            }

            return false;
        }

        private handleLeadingWhitespace(): boolean {
            if (this._lastCheckpoint === this._index && this._text[this._index].trim().length === 0) {
                this._lastCheckpoint++;
                return true;
            }

            return false;
        }
    }

    enum CssToken {
        Selector,
        Media,
        Property,
        Value
    };
}