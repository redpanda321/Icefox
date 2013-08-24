# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is mozilla.org code.
#
# The Initial Developer of the Original Code is
# Netscape Communications Corporation.
# Portions created by the Initial Developer are Copyright (C) 1998
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Blake Ross <blake@cs.stanford.edu>
#   David Hyatt <hyatt@mozilla.org>
#   Peter Annema <disttsc@bart.nl>
#   Dean Tessman <dean_tessman@hotmail.com>
#   Kevin Puetz <puetzk@iastate.edu>
#   Ben Goodger <ben@netscape.com>
#   Pierre Chanial <chanial@noos.fr>
#   Jason Eager <jce2@po.cwru.edu>
#   Joe Hewitt <hewitt@netscape.com>
#   Alec Flett <alecf@netscape.com>
#   Asaf Romano <mozilla.mano@sent.com>
#   Jason Barnabe <jason_barnabe@fastmail.fm>
#   Peter Parente <parente@cs.unc.edu>
#   Giorgio Maone <g.maone@informaction.com>
#   Tom Germeau <tom.germeau@epigoon.com>
#   Jesse Ruderman <jruderman@gmail.com>
#   Joe Hughes <joe@retrovirus.com>
#   Pamela Greene <pamg.bugs@gmail.com>
#   Michael Ventnor <ventnors_dogs234@yahoo.com.au>
#   Simon Bünzli <zeniko@gmail.com>
#   Gijs Kruitbosch <gijskruitbosch@gmail.com>
#   Ehsan Akhgari <ehsan.akhgari@gmail.com>
#   Dan Mosedale <dmose@mozilla.org>
#   Justin Dolske <dolske@mozilla.com>
#   Kathleen Brade <brade@pearlcrescent.com>
#   Mark Smith <mcs@pearlcrescent.com>
#
# Alternatively, the contents of this file may be used under the terms of
# either the GNU General Public License Version 2 or later (the "GPL"), or
# the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****

function nsContextMenu(aXulMenu, aBrowser) {
  this.shouldDisplay = true;
  this.initMenu(aBrowser);
}

// Prototype for nsContextMenu "class."
nsContextMenu.prototype = {
  initMenu: function CM_initMenu(aBrowser) {
    // Get contextual info.
    this.setTarget(document.popupNode, document.popupRangeParent,
                   document.popupRangeOffset);
    if (!this.shouldDisplay)
      return;

    this.browser = aBrowser;
    this.isFrameImage = document.getElementById("isFrameImage");
    this.ellipsis = "\u2026";
    try {
      this.ellipsis = gPrefService.getComplexValue("intl.ellipsis",
                                                   Ci.nsIPrefLocalizedString).data;
    } catch (e) { }
    this.isTextSelected = this.isTextSelection();
    this.isContentSelected = this.isContentSelection();

    // Initialize (disable/remove) menu items.
    this.initItems();
  },

  initItems: function CM_initItems() {
    this.initOpenItems();
    this.initNavigationItems();
    this.initViewItems();
    this.initMiscItems();
    this.initSpellingItems();
    this.initSaveItems();
    this.initClipboardItems();
    this.initMediaPlayerItems();
  },

  initOpenItems: function CM_initOpenItems() {
    var isMailtoInternal = false;
    if (this.onMailtoLink) {
      var mailtoHandler = Cc["@mozilla.org/uriloader/external-protocol-service;1"].
                          getService(Ci.nsIExternalProtocolService).
                          getProtocolHandlerInfo("mailto");
      isMailtoInternal = (!mailtoHandler.alwaysAskBeforeHandling &&
                          mailtoHandler.preferredAction == Ci.nsIHandlerInfo.useHelperApp &&
                          (mailtoHandler.preferredApplicationHandler instanceof Ci.nsIWebHandlerApp));
    }

    // Time to do some bad things and see if we've highlighted a URL that
    // isn't actually linked.
    var onPlainTextLink = false;
    if (this.isTextSelected && !this.onLink) {
      // Ok, we have some text, let's figure out if it looks like a URL.
      let selection =  document.commandDispatcher.focusedWindow
                               .getSelection();
      let linkText = selection.toString().trim();
      let uri;
      if (/^(?:https?|ftp):/i.test(linkText)) {
        try {
          uri = makeURI(linkText);
        } catch (ex) {}
      }
      // Check if this could be a valid url, just missing the protocol.
      else if (/^(?:\w+\.)+\D\S*$/.test(linkText)) {
        // Now let's see if this is an intentional link selection. Our guess is
        // based on whether the selection begins/ends with whitespace or is
        // preceded/followed by a non-word character.

        // selection.toString() trims trailing whitespace, so we look for
        // that explicitly in the first and last ranges.
        let beginRange = selection.getRangeAt(0);
        let delimitedAtStart = /^\s/.test(beginRange);
        if (!delimitedAtStart) {
          let container = beginRange.startContainer;
          let offset = beginRange.startOffset;
          if (container.nodeType == Node.TEXT_NODE && offset > 0)
            delimitedAtStart = /\W/.test(container.textContent[offset - 1]);
          else
            delimitedAtStart = true;
        }

        let delimitedAtEnd = false;
        if (delimitedAtStart) {
          let endRange = selection.getRangeAt(selection.rangeCount - 1);
          delimitedAtEnd = /\s$/.test(endRange);
          if (!delimitedAtEnd) {
            let container = endRange.endContainer;
            let offset = endRange.endOffset;
            if (container.nodeType == Node.TEXT_NODE &&
                offset < container.textContent.length)
              delimitedAtEnd = /\W/.test(container.textContent[offset]);
            else
              delimitedAtEnd = true;
          }
        }

        if (delimitedAtStart && delimitedAtEnd) {
          let uriFixup = Cc["@mozilla.org/docshell/urifixup;1"]
                           .getService(Ci.nsIURIFixup);
          try {
            uri = uriFixup.createFixupURI(linkText, uriFixup.FIXUP_FLAG_NONE);
          } catch (ex) {}
        }
      }

      if (uri && uri.host) {
        this.linkURI = uri;
        this.linkURL = this.linkURI.spec;
        onPlainTextLink = true;
      }
    }

    var shouldShow = this.onSaveableLink || isMailtoInternal || onPlainTextLink;
    this.showItem("context-openlink", shouldShow);
    this.showItem("context-openlinkintab", shouldShow);
    this.showItem("context-openlinkincurrent", onPlainTextLink);
    this.showItem("context-sep-open", shouldShow);
  },

  initNavigationItems: function CM_initNavigationItems() {
    var shouldShow = !(this.isContentSelected || this.onLink || this.onImage ||
                       this.onCanvas || this.onVideo || this.onAudio ||
                       this.onTextInput);
    this.showItem("context-back", shouldShow);
    this.showItem("context-forward", shouldShow);
    this.showItem("context-reload", shouldShow);
    this.showItem("context-stop", shouldShow);
    this.showItem("context-sep-stop", shouldShow);

    // XXX: Stop is determined in browser.js; the canStop broadcaster is broken
    //this.setItemAttrFromNode( "context-stop", "disabled", "canStop" );
  },

  initSaveItems: function CM_initSaveItems() {
    var shouldShow = !(this.onTextInput || this.onLink ||
                       this.isContentSelected || this.onImage ||
                       this.onCanvas || this.onVideo || this.onAudio);
    this.showItem("context-savepage", shouldShow);
    this.showItem("context-sendpage", shouldShow);

    // Save+Send link depends on whether we're in a link.
    this.showItem("context-savelink", this.onSaveableLink);
    this.showItem("context-sendlink", this.onSaveableLink);

    // Save image depends on having loaded its content, video and audio don't.
    this.showItem("context-saveimage", this.onLoadedImage || this.onCanvas);
    this.showItem("context-savevideo", this.onVideo);
    this.showItem("context-saveaudio", this.onAudio);
    this.setItemAttr("context-savevideo", "disabled", !this.mediaURL);
    this.setItemAttr("context-saveaudio", "disabled", !this.mediaURL);
    // Send media URL (but not for canvas, since it's a big data: URL)
    this.showItem("context-sendimage", this.onImage);
    this.showItem("context-sendvideo", this.onVideo);
    this.showItem("context-sendaudio", this.onAudio);
    this.setItemAttr("context-sendvideo", "disabled", !this.mediaURL);
    this.setItemAttr("context-sendaudio", "disabled", !this.mediaURL);
  },

  initViewItems: function CM_initViewItems() {
    // View source is always OK, unless in directory listing.
    this.showItem("context-viewpartialsource-selection",
                  this.isContentSelected);
    this.showItem("context-viewpartialsource-mathml",
                  this.onMathML && !this.isContentSelected);

    var shouldShow = !(this.isContentSelected ||
                       this.onImage || this.onCanvas ||
                       this.onVideo || this.onAudio ||
                       this.onLink || this.onTextInput);
    this.showItem("context-viewsource", shouldShow);
    this.showItem("context-viewinfo", shouldShow);

    this.showItem("context-sep-viewsource", shouldShow);

    // Set as Desktop background depends on whether an image was clicked on,
    // and only works if we have a shell service.
    var haveSetDesktopBackground = false;
#ifdef HAVE_SHELL_SERVICE
    // Only enable Set as Desktop Background if we can get the shell service.
    var shell = getShellService();
    if (shell)
      haveSetDesktopBackground = true;
#endif
    this.showItem("context-setDesktopBackground",
                  haveSetDesktopBackground && this.onLoadedImage);

    if (haveSetDesktopBackground && this.onLoadedImage) {
      document.getElementById("context-setDesktopBackground")
              .disabled = this.disableSetDesktopBackground();
    }

    // Reload image depends on an image that's not fully loaded
    this.showItem("context-reloadimage", (this.onImage && !this.onCompletedImage));

    // View image depends on having an image that's not standalone
    // (or is in a frame), or a canvas.
    this.showItem("context-viewimage", (this.onImage &&
                  (!this.onStandaloneImage || this.inFrame)) || this.onCanvas);

    this.showItem("context-viewvideo", this.onVideo);
    this.setItemAttr("context-viewvideo",  "disabled", !this.mediaURL);

    // View background image depends on whether there is one.
    this.showItem("context-viewbgimage", shouldShow && !this._hasMultipleBGImages);
    this.showItem("context-sep-viewbgimage", shouldShow && !this._hasMultipleBGImages);
    document.getElementById("context-viewbgimage")
            .disabled = !this.hasBGImage;

    this.showItem("context-viewimageinfo", this.onImage);
  },

  initMiscItems: function CM_initMiscItems() {
    var isTextSelected = this.isTextSelected;

    // Use "Bookmark This Link" if on a link.
    this.showItem("context-bookmarkpage",
                  !(this.isContentSelected || this.onTextInput || this.onLink ||
                    this.onImage || this.onVideo || this.onAudio));
    this.showItem("context-bookmarklink", this.onLink && !this.onMailtoLink);
    this.showItem("context-searchselect", isTextSelected);
    this.showItem("context-keywordfield",
                  this.onTextInput && this.onKeywordField);
    this.showItem("frame", this.inFrame);
    this.showItem("frame-sep", this.inFrame && isTextSelected);

    // Hide menu entries for images, show otherwise
    if (this.inFrame) {
      if (mimeTypeIsTextBased(this.target.ownerDocument.contentType))
        this.isFrameImage.removeAttribute('hidden');
      else
        this.isFrameImage.setAttribute('hidden', 'true');
    }

    // BiDi UI
    this.showItem("context-sep-bidi", top.gBidiUI);
    this.showItem("context-bidi-text-direction-toggle",
                  this.onTextInput && top.gBidiUI);
    this.showItem("context-bidi-page-direction-toggle",
                  !this.onTextInput && top.gBidiUI);
  },

  initSpellingItems: function() {
    var canSpell = InlineSpellCheckerUI.canSpellCheck;
    var onMisspelling = InlineSpellCheckerUI.overMisspelling;
    this.showItem("spell-check-enabled", canSpell);
    this.showItem("spell-separator", canSpell || this.onEditableArea);
    if (canSpell) {
      document.getElementById("spell-check-enabled")
              .setAttribute("checked", InlineSpellCheckerUI.enabled);
    }

    this.showItem("spell-add-to-dictionary", onMisspelling);

    // suggestion list
    this.showItem("spell-suggestions-separator", onMisspelling);
    if (onMisspelling) {
      var suggestionsSeparator =
        document.getElementById("spell-add-to-dictionary");
      var numsug =
        InlineSpellCheckerUI.addSuggestionsToMenu(suggestionsSeparator.parentNode,
                                                  suggestionsSeparator, 5);
      this.showItem("spell-no-suggestions", numsug == 0);
    }
    else
      this.showItem("spell-no-suggestions", false);

    // dictionary list
    this.showItem("spell-dictionaries", InlineSpellCheckerUI.enabled);
    if (canSpell) {
      var dictMenu = document.getElementById("spell-dictionaries-menu");
      var dictSep = document.getElementById("spell-language-separator");
      InlineSpellCheckerUI.addDictionaryListToMenu(dictMenu, dictSep);
      this.showItem("spell-add-dictionaries-main", false);
    }
    else if (this.onEditableArea) {
      // when there is no spellchecker but we might be able to spellcheck
      // add the add to dictionaries item. This will ensure that people
      // with no dictionaries will be able to download them
      this.showItem("spell-add-dictionaries-main", true);
    }
    else
      this.showItem("spell-add-dictionaries-main", false);
  },

  initClipboardItems: function() {
    // Copy depends on whether there is selected text.
    // Enabling this context menu item is now done through the global
    // command updating system
    // this.setItemAttr( "context-copy", "disabled", !this.isTextSelected() );
    goUpdateGlobalEditMenuItems();

    this.showItem("context-undo", this.onTextInput);
    this.showItem("context-sep-undo", this.onTextInput);
    this.showItem("context-cut", this.onTextInput);
    this.showItem("context-copy",
                  this.isContentSelected || this.onTextInput);
    this.showItem("context-paste", this.onTextInput);
    this.showItem("context-delete", this.onTextInput);
    this.showItem("context-sep-paste", this.onTextInput);
    this.showItem("context-selectall", !(this.onLink || this.onImage ||
                  this.onVideo || this.onAudio) || this.isDesignMode);
    this.showItem("context-sep-selectall", this.isContentSelected );

    // XXX dr
    // ------
    // nsDocumentViewer.cpp has code to determine whether we're
    // on a link or an image. we really ought to be using that...

    // Copy email link depends on whether we're on an email link.
    this.showItem("context-copyemail", this.onMailtoLink);

    // Copy link location depends on whether we're on a non-mailto link.
    this.showItem("context-copylink", this.onLink && !this.onMailtoLink);
    this.showItem("context-sep-copylink", this.onLink &&
                  (this.onImage || this.onVideo || this.onAudio));

#ifdef CONTEXT_COPY_IMAGE_CONTENTS
    // Copy image contents depends on whether we're on an image.
    this.showItem("context-copyimage-contents", this.onImage);
#endif
    // Copy image location depends on whether we're on an image.
    this.showItem("context-copyimage", this.onImage);
    this.showItem("context-copyvideourl", this.onVideo);
    this.showItem("context-copyaudiourl", this.onAudio);
    this.setItemAttr("context-copyvideourl",  "disabled", !this.mediaURL);
    this.setItemAttr("context-copyaudiourl",  "disabled", !this.mediaURL);
    this.showItem("context-sep-copyimage", this.onImage ||
                  this.onVideo || this.onAudio);
  },

  initMediaPlayerItems: function() {
    var onMedia = (this.onVideo || this.onAudio);
    // Several mutually exclusive items... play/pause, mute/unmute, show/hide
    this.showItem("context-media-play",  onMedia && (this.target.paused || this.target.ended));
    this.showItem("context-media-pause", onMedia && !this.target.paused && !this.target.ended);
    this.showItem("context-media-mute",   onMedia && !this.target.muted);
    this.showItem("context-media-unmute", onMedia && this.target.muted);
    this.showItem("context-media-showcontrols", onMedia && !this.target.controls);
    this.showItem("context-media-hidecontrols", onMedia && this.target.controls);
    this.showItem("context-video-fullscreen", this.onVideo);
    // Disable them when there isn't a valid media source loaded.
    if (onMedia) {
      var hasError = (this.target.error != null);
      this.setItemAttr("context-media-play",  "disabled", hasError);
      this.setItemAttr("context-media-pause", "disabled", hasError);
      this.setItemAttr("context-media-mute",   "disabled", hasError);
      this.setItemAttr("context-media-unmute", "disabled", hasError);
      this.setItemAttr("context-media-showcontrols", "disabled", hasError);
      this.setItemAttr("context-media-hidecontrols", "disabled", hasError);
      if (this.onVideo)
        this.setItemAttr("context-video-fullscreen",  "disabled", hasError);
    }
    this.showItem("context-media-sep-commands",  onMedia);
  },

  // Set various context menu attributes based on the state of the world.
  setTarget: function (aNode, aRangeParent, aRangeOffset) {
    const xulNS = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";
    if (aNode.namespaceURI == xulNS ||
        aNode.nodeType == Node.DOCUMENT_NODE ||
        this.isTargetAFormControl(aNode)) {
      this.shouldDisplay = false;
      return;
    }

    // Initialize contextual info.
    this.onImage           = false;
    this.onLoadedImage     = false;
    this.onCompletedImage  = false;
    this.onStandaloneImage = false;
    this.onCanvas          = false;
    this.onVideo           = false;
    this.onAudio           = false;
    this.onTextInput       = false;
    this.onKeywordField    = false;
    this.mediaURL          = "";
    this.onLink            = false;
    this.onMailtoLink      = false;
    this.onSaveableLink    = false;
    this.link              = null;
    this.linkURL           = "";
    this.linkURI           = null;
    this.linkProtocol      = "";
    this.onMathML          = false;
    this.inFrame           = false;
    this.hasBGImage        = false;
    this.bgImageURL        = "";
    this.onEditableArea    = false;
    this.isDesignMode      = false;

    // Clear any old spellchecking items from the menu, this used to
    // be in the menu hiding code but wasn't getting called in all
    // situations. Here, we can ensure it gets cleaned up any time the
    // menu is shown. Note: must be before uninit because that clears the
    // internal vars
    InlineSpellCheckerUI.clearSuggestionsFromMenu();
    InlineSpellCheckerUI.clearDictionaryListFromMenu();

    InlineSpellCheckerUI.uninit();

    // Remember the node that was clicked.
    this.target = aNode;

    // First, do checks for nodes that never have children.
    if (this.target.nodeType == Node.ELEMENT_NODE) {
      // See if the user clicked on an image.
      if (this.target instanceof Ci.nsIImageLoadingContent &&
          this.target.currentURI) {
        this.onImage = true;

        var request =
          this.target.getRequest(Ci.nsIImageLoadingContent.CURRENT_REQUEST);
        if (request && (request.imageStatus & request.STATUS_SIZE_AVAILABLE))
          this.onLoadedImage = true;
        if (request && (request.imageStatus & request.STATUS_LOAD_COMPLETE))
          this.onCompletedImage = true;

        this.mediaURL = this.target.currentURI.spec;
        if (this.target.ownerDocument instanceof ImageDocument)
          this.onStandaloneImage = true;
      }
      else if (this.target instanceof HTMLCanvasElement) {
        this.onCanvas = true;
      }
      else if (this.target instanceof HTMLVideoElement) {
        this.onVideo = true;
        this.mediaURL = this.target.currentSrc || this.target.src;
      }
      else if (this.target instanceof HTMLAudioElement) {
        this.onAudio = true;
        this.mediaURL = this.target.currentSrc || this.target.src;
      }
      else if (this.target instanceof HTMLInputElement ) {
        this.onTextInput = this.isTargetATextBox(this.target);
        // allow spellchecking UI on all writable text boxes except passwords
        if (this.onTextInput && ! this.target.readOnly &&
            this.target.type != "password") {
          this.onEditableArea = true;
          InlineSpellCheckerUI.init(this.target.QueryInterface(Ci.nsIDOMNSEditableElement).editor);
          InlineSpellCheckerUI.initFromEvent(aRangeParent, aRangeOffset);
        }
        this.onKeywordField = this.isTargetAKeywordField(this.target);
      }
      else if (this.target instanceof HTMLTextAreaElement) {
        this.onTextInput = true;
        if (!this.target.readOnly) {
          this.onEditableArea = true;
          InlineSpellCheckerUI.init(this.target.QueryInterface(Ci.nsIDOMNSEditableElement).editor);
          InlineSpellCheckerUI.initFromEvent(aRangeParent, aRangeOffset);
        }
      }
      else if (this.target instanceof HTMLHtmlElement) {
        var bodyElt = this.target.ownerDocument.body;
        if (bodyElt) {
          let computedURL;
          try {
            computedURL = this.getComputedURL(bodyElt, "background-image");
            this._hasMultipleBGImages = false;
          } catch (e) {
            this._hasMultipleBGImages = true;
          }
          if (computedURL) {
            this.hasBGImage = true;
            this.bgImageURL = makeURLAbsolute(bodyElt.baseURI,
                                              computedURL);
          }
        }
      }
    }

    // Second, bubble out, looking for items of interest that can have childen.
    // Always pick the innermost link, background image, etc.
    const XMLNS = "http://www.w3.org/XML/1998/namespace";
    var elem = this.target;
    while (elem) {
      if (elem.nodeType == Node.ELEMENT_NODE) {
        // Link?
        if (!this.onLink &&
             ((elem instanceof HTMLAnchorElement && elem.href) ||
              (elem instanceof HTMLAreaElement && elem.href) ||
              elem instanceof HTMLLinkElement ||
              elem.getAttributeNS("http://www.w3.org/1999/xlink", "type") == "simple")) {

          // Target is a link or a descendant of a link.
          this.onLink = true;

          // xxxmpc: this is kind of a hack to work around a Gecko bug (see bug 266932)
          // we're going to walk up the DOM looking for a parent link node,
          // this shouldn't be necessary, but we're matching the existing behaviour for left click
          var realLink = elem;
          var parent = elem;
          while ((parent = parent.parentNode) &&
                 (parent.nodeType == Node.ELEMENT_NODE)) {
            try {
              if ((parent instanceof HTMLAnchorElement && parent.href) ||
                  (parent instanceof HTMLAreaElement && parent.href) ||
                  parent instanceof HTMLLinkElement ||
                  parent.getAttributeNS("http://www.w3.org/1999/xlink", "type") == "simple")
                realLink = parent;
            } catch (e) { }
          }

          // Remember corresponding element.
          this.link = realLink;
          this.linkURL = this.getLinkURL();
          this.linkURI = this.getLinkURI();
          this.linkProtocol = this.getLinkProtocol();
          this.onMailtoLink = (this.linkProtocol == "mailto");
          this.onSaveableLink = this.isLinkSaveable( this.link );
        }

        // Background image?  Don't bother if we've already found a
        // background image further down the hierarchy.  Otherwise,
        // we look for the computed background-image style.
        if (!this.hasBGImage &&
            !this._hasMultipleBGImages) {
          let bgImgUrl;
          try {
            bgImgUrl = this.getComputedURL(elem, "background-image");
            this._hasMultipleBGImages = false;
          } catch (e) {
            this._hasMultipleBGImages = true;
          }
          if (bgImgUrl) {
            this.hasBGImage = true;
            this.bgImageURL = makeURLAbsolute(elem.baseURI,
                                              bgImgUrl);
          }
        }
      }

      elem = elem.parentNode;
    }

    // See if the user clicked on MathML
    const NS_MathML = "http://www.w3.org/1998/Math/MathML";
    if ((this.target.nodeType == Node.TEXT_NODE &&
         this.target.parentNode.namespaceURI == NS_MathML)
         || (this.target.namespaceURI == NS_MathML))
      this.onMathML = true;

    // See if the user clicked in a frame.
    var docDefaultView = this.target.ownerDocument.defaultView;
    if (docDefaultView != docDefaultView.top)
      this.inFrame = true;

    // if the document is editable, show context menu like in text inputs
    if (!this.onEditableArea) {
      var win = this.target.ownerDocument.defaultView;
      if (win) {
        var isEditable = false;
        try {
          var editingSession = win.QueryInterface(Ci.nsIInterfaceRequestor)
                                  .getInterface(Ci.nsIWebNavigation)
                                  .QueryInterface(Ci.nsIInterfaceRequestor)
                                  .getInterface(Ci.nsIEditingSession);
          if (editingSession.windowIsEditable(win) &&
              this.getComputedStyle(this.target, "-moz-user-modify") == "read-write") {
            isEditable = true;
          }
        }
        catch(ex) {
          // If someone built with composer disabled, we can't get an editing session.
        }

        if (isEditable) {
          this.onTextInput       = true;
          this.onKeywordField    = false;
          this.onImage           = false;
          this.onLoadedImage     = false;
          this.onCompletedImage  = false;
          this.onMathML          = false;
          this.inFrame           = false;
          this.hasBGImage        = false;
          this.isDesignMode      = true;
          this.onEditableArea = true;
          InlineSpellCheckerUI.init(editingSession.getEditorForWindow(win));
          var canSpell = InlineSpellCheckerUI.canSpellCheck;
          InlineSpellCheckerUI.initFromEvent(aRangeParent, aRangeOffset);
          this.showItem("spell-check-enabled", canSpell);
          this.showItem("spell-separator", canSpell);
        }
      }
    }
  },

  // Returns the computed style attribute for the given element.
  getComputedStyle: function(aElem, aProp) {
    return aElem.ownerDocument
                .defaultView
                .getComputedStyle(aElem, "").getPropertyValue(aProp);
  },

  // Returns a "url"-type computed style attribute value, with the url() stripped.
  getComputedURL: function(aElem, aProp) {
    var url = aElem.ownerDocument
                   .defaultView.getComputedStyle(aElem, "")
                   .getPropertyCSSValue(aProp);
    if (url instanceof CSSValueList) {
      if (url.length != 1)
        throw "found multiple URLs";
      url = url[0];
    }
    return url.primitiveType == CSSPrimitiveValue.CSS_URI ?
           url.getStringValue() : null;
  },

  // Returns true if clicked-on link targets a resource that can be saved.
  isLinkSaveable: function(aLink) {
    // We don't do the Right Thing for news/snews yet, so turn them off
    // until we do.
    return this.linkProtocol && !(
             this.linkProtocol == "mailto"     ||
             this.linkProtocol == "javascript" ||
             this.linkProtocol == "news"       ||
             this.linkProtocol == "snews"      );
  },

  // Open linked-to URL in a new window.
  openLink : function () {
    openNewWindowWith(this.linkURL, this.target.ownerDocument, null, false);
  },

  // Open linked-to URL in a new tab.
  openLinkInTab: function() {
    openNewTabWith(this.linkURL, this.target.ownerDocument, null, null, false);
  },

  // open URL in current tab
  openLinkInCurrent: function() {
    openUILinkIn(this.linkURL, "current", null, null, 
                 this.target.ownerDocument.documentURIObject);
  },

  // Open frame in a new tab.
  openFrameInTab: function() {
    var doc = this.target.ownerDocument;
    var frameURL = doc.location.href;
    var referrer = doc.referrer;

    return openNewTabWith(frameURL, null, null, null, false,
                          referrer ? makeURI(referrer) : null);
  },

  // Reload clicked-in frame.
  reloadFrame: function() {
    this.target.ownerDocument.location.reload();
  },

  // Open clicked-in frame in its own window.
  openFrame: function() {
    var doc = this.target.ownerDocument;
    var frameURL = doc.location.href;
    var referrer = doc.referrer;

    return openNewWindowWith(frameURL, null, null, false,
                             referrer ? makeURI(referrer) : null);
  },

  // Open clicked-in frame in the same window.
  showOnlyThisFrame: function() {
    var doc = this.target.ownerDocument;
    var frameURL = doc.location.href;

    urlSecurityCheck(frameURL, this.browser.contentPrincipal,
                     Ci.nsIScriptSecurityManager.DISALLOW_SCRIPT);
    var referrer = doc.referrer;
    this.browser.loadURI(frameURL, referrer ? makeURI(referrer) : null);
  },

  // View Partial Source
  viewPartialSource: function(aContext) {
    var focusedWindow = document.commandDispatcher.focusedWindow;
    if (focusedWindow == window)
      focusedWindow = content;

    var docCharset = null;
    if (focusedWindow)
      docCharset = "charset=" + focusedWindow.document.characterSet;

    // "View Selection Source" and others such as "View MathML Source"
    // are mutually exclusive, with the precedence given to the selection
    // when there is one
    var reference = null;
    if (aContext == "selection")
      reference = focusedWindow.getSelection();
    else if (aContext == "mathml")
      reference = this.target;
    else
      throw "not reached";

    // unused (and play nice for fragments generated via XSLT too)
    var docUrl = null;
    window.openDialog("chrome://global/content/viewPartialSource.xul",
                      "_blank", "scrollbars,resizable,chrome,dialog=no",
                      docUrl, docCharset, reference, aContext);
  },

  // Open new "view source" window with the frame's URL.
  viewFrameSource: function() {
    BrowserViewSourceOfDocument(this.target.ownerDocument);
  },

  viewInfo: function() {
    BrowserPageInfo(this.target.ownerDocument.defaultView.top.document);
  },

  viewImageInfo: function() {
    BrowserPageInfo(this.target.ownerDocument.defaultView.top.document,
                    "mediaTab", this.target);
  },

  viewFrameInfo: function() {
    BrowserPageInfo(this.target.ownerDocument);
  },

  reloadImage: function(e) {
    urlSecurityCheck(this.mediaURL,
                     this.browser.contentPrincipal,
                     Ci.nsIScriptSecurityManager.DISALLOW_SCRIPT);

    if (this.target instanceof Ci.nsIImageLoadingContent)
      this.target.forceReload();
  },

  // Change current window to the URL of the image, video, or audio.
  viewMedia: function(e) {
    var viewURL;

    if (this.onCanvas)
      viewURL = this.target.toDataURL();
    else {
      viewURL = this.mediaURL;
      urlSecurityCheck(viewURL,
                       this.browser.contentPrincipal,
                       Ci.nsIScriptSecurityManager.DISALLOW_SCRIPT);
    }

    var doc = this.target.ownerDocument;
    openUILink(viewURL, e, null, null, null, null, doc.documentURIObject );
  },

  fullScreenVideo: function () {
    this.target.pause();

    openDialog("chrome://browser/content/fullscreen-video.xhtml",
               "", "chrome,centerscreen,dialog=no", this.target);
  },

  // Change current window to the URL of the background image.
  viewBGImage: function(e) {
    urlSecurityCheck(this.bgImageURL,
                     this.browser.contentPrincipal,
                     Ci.nsIScriptSecurityManager.DISALLOW_SCRIPT);
    var doc = this.target.ownerDocument;
    openUILink(this.bgImageURL, e, null, null, null, null, doc.documentURIObject );
  },

  disableSetDesktopBackground: function() {
    // Disable the Set as Desktop Background menu item if we're still trying
    // to load the image or the load failed.
    if (!(this.target instanceof Ci.nsIImageLoadingContent))
      return true;

    if (("complete" in this.target) && !this.target.complete)
      return true;

    if (this.target.currentURI.schemeIs("javascript"))
      return true;

    var request = this.target
                      .QueryInterface(Ci.nsIImageLoadingContent)
                      .getRequest(Ci.nsIImageLoadingContent.CURRENT_REQUEST);
    if (!request)
      return true;

    return false;
  },

  setDesktopBackground: function() {
    // Paranoia: check disableSetDesktopBackground again, in case the
    // image changed since the context menu was initiated.
    if (this.disableSetDesktopBackground())
      return;

    urlSecurityCheck(this.target.currentURI.spec,
                     this.target.ownerDocument.nodePrincipal);

    // Confirm since it's annoying if you hit this accidentally.
    const kDesktopBackgroundURL = 
                  "chrome://browser/content/setDesktopBackground.xul";
#ifdef XP_MACOSX
    // On Mac, the Set Desktop Background window is not modal.
    // Don't open more than one Set Desktop Background window.
    var wm = Components.classes["@mozilla.org/appshell/window-mediator;1"]
                       .getService(Components.interfaces.nsIWindowMediator);
    var dbWin = wm.getMostRecentWindow("Shell:SetDesktopBackground");
    if (dbWin) {
      dbWin.gSetBackground.init(this.target);
      dbWin.focus();
    }
    else {
      openDialog(kDesktopBackgroundURL, "",
                 "centerscreen,chrome,dialog=no,dependent,resizable=no",
                 this.target);
    }
#else
    // On non-Mac platforms, the Set Wallpaper dialog is modal.
    openDialog(kDesktopBackgroundURL, "",
               "centerscreen,chrome,dialog,modal,dependent",
               this.target);
#endif
  },

  // Save URL of clicked-on frame.
  saveFrame: function () {
    saveDocument(this.target.ownerDocument);
  },

  // Save URL of clicked-on link.
  saveLink: function() {
    // canonical def in nsURILoader.h
    const NS_ERROR_SAVE_LINK_AS_TIMEOUT = 0x805d0020;

    var doc =  this.target.ownerDocument;
    urlSecurityCheck(this.linkURL, doc.nodePrincipal);
    var linkText = this.linkText();
    var linkURL = this.linkURL;


    // an object to proxy the data through to
    // nsIExternalHelperAppService.doContent, which will wait for the
    // appropriate MIME-type headers and then prompt the user with a
    // file picker
    function saveAsListener() {}
    saveAsListener.prototype = {
      extListener: null, 

      onStartRequest: function saveLinkAs_onStartRequest(aRequest, aContext) {

        // if the timer fired, the error status will have been caused by that,
        // and we'll be restarting in onStopRequest, so no reason to notify
        // the user
        if (aRequest.status == NS_ERROR_SAVE_LINK_AS_TIMEOUT)
          return;

        timer.cancel();

        // some other error occured; notify the user...
        if (!Components.isSuccessCode(aRequest.status)) {
          try {
            const sbs = Cc["@mozilla.org/intl/stringbundle;1"].
                        getService(Ci.nsIStringBundleService);
            const bundle = sbs.createBundle(
                    "chrome://mozapps/locale/downloads/downloads.properties");

            const title = bundle.GetStringFromName("downloadErrorAlertTitle");
            const msg = bundle.GetStringFromName("downloadErrorGeneric");

            const promptSvc = Cc["@mozilla.org/embedcomp/prompt-service;1"].
                              getService(Ci.nsIPromptService);
            promptSvc.alert(doc.defaultView, title, msg);
          } catch (ex) {}
          return;
        }

        var extHelperAppSvc = 
          Cc["@mozilla.org/uriloader/external-helper-app-service;1"].
          getService(Ci.nsIExternalHelperAppService);
        var channel = aRequest.QueryInterface(Ci.nsIChannel);
        this.extListener = 
          extHelperAppSvc.doContent(channel.contentType, aRequest, 
                                    doc.defaultView, true);
        this.extListener.onStartRequest(aRequest, aContext);
      }, 

      onStopRequest: function saveLinkAs_onStopRequest(aRequest, aContext, 
                                                       aStatusCode) {
        if (aStatusCode == NS_ERROR_SAVE_LINK_AS_TIMEOUT) {
          // do it the old fashioned way, which will pick the best filename
          // it can without waiting.
          saveURL(linkURL, linkText, null, true, false, doc.documentURIObject);
        }
        if (this.extListener)
          this.extListener.onStopRequest(aRequest, aContext, aStatusCode);
      },

      onDataAvailable: function saveLinkAs_onDataAvailable(aRequest, aContext,
                                                           aInputStream,
                                                           aOffset, aCount) {
        this.extListener.onDataAvailable(aRequest, aContext, aInputStream,
                                         aOffset, aCount);
      }
    }

    // in case we need to prompt the user for authentication
    function callbacks() {}
    callbacks.prototype = {
      getInterface: function sLA_callbacks_getInterface(aIID) {
        if (aIID.equals(Ci.nsIAuthPrompt) || aIID.equals(Ci.nsIAuthPrompt2)) {
          var ww = Cc["@mozilla.org/embedcomp/window-watcher;1"].
                   getService(Ci.nsIPromptFactory);
          return ww.getPrompt(doc.defaultView, aIID);
        }
        throw Cr.NS_ERROR_NO_INTERFACE;
      } 
    }

    // if it we don't have the headers after a short time, the user 
    // won't have received any feedback from their click.  that's bad.  so
    // we give up waiting for the filename. 
    function timerCallback() {}
    timerCallback.prototype = {
      notify: function sLA_timer_notify(aTimer) {
        channel.cancel(NS_ERROR_SAVE_LINK_AS_TIMEOUT);
        return;
      }
    }

    // set up a channel to do the saving
    var ioService = Cc["@mozilla.org/network/io-service;1"].
                    getService(Ci.nsIIOService);
    var channel = ioService.newChannelFromURI(this.getLinkURI());
    channel.notificationCallbacks = new callbacks();
    channel.loadFlags |= Ci.nsIRequest.LOAD_BYPASS_CACHE |
                         Ci.nsIChannel.LOAD_CALL_CONTENT_SNIFFERS;
    if (channel instanceof Ci.nsIHttpChannel) {
      channel.referrer = doc.documentURIObject;
      if (channel instanceof Ci.nsIHttpChannelInternal)
        channel.forceAllowThirdPartyCookie = true;
    }

    // fallback to the old way if we don't see the headers quickly 
    var timeToWait = 
      gPrefService.getIntPref("browser.download.saveLinkAsFilenameTimeout");
    var timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    timer.initWithCallback(new timerCallback(), timeToWait,
                           timer.TYPE_ONE_SHOT);

    // kick off the channel with our proxy object as the listener
    channel.asyncOpen(new saveAsListener(), null);
  },

  sendLink: function() {
    // we don't know the title of the link so pass in an empty string
    MailIntegration.sendMessage( this.linkURL, "" );
  },

  // Backwards-compatability wrapper
  saveImage : function() {
    if (this.onCanvas || this.onImage)
        this.saveMedia();
  },

  // Save URL of the clicked upon image, video, or audio.
  saveMedia: function() {
    var doc =  this.target.ownerDocument;
    if (this.onCanvas) {
      // Bypass cache, since it's a data: URL.
      saveImageURL(this.target.toDataURL(), "canvas.png", "SaveImageTitle",
                   true, false, doc.documentURIObject);
    }
    else if (this.onImage) {
      urlSecurityCheck(this.mediaURL, doc.nodePrincipal);
      saveImageURL(this.mediaURL, null, "SaveImageTitle", false,
                   false, doc.documentURIObject);
    }
    else if (this.onVideo || this.onAudio) {
      urlSecurityCheck(this.mediaURL, doc.nodePrincipal);
      var dialogTitle = this.onVideo ? "SaveVideoTitle" : "SaveAudioTitle";
      saveURL(this.mediaURL, null, dialogTitle, false,
              false, doc.documentURIObject);
    }
  },

  // Backwards-compatability wrapper
  sendImage : function() {
    if (this.onCanvas || this.onImage)
        this.sendMedia();
  },

  sendMedia: function() {
    MailIntegration.sendMessage(this.mediaURL, "");
  },

  // Generate email address and put it on clipboard.
  copyEmail: function() {
    // Copy the comma-separated list of email addresses only.
    // There are other ways of embedding email addresses in a mailto:
    // link, but such complex parsing is beyond us.
    var url = this.linkURL;
    var qmark = url.indexOf("?");
    var addresses;

    // 7 == length of "mailto:"
    addresses = qmark > 7 ? url.substring(7, qmark) : url.substr(7);

    // Let's try to unescape it using a character set
    // in case the address is not ASCII.
    try {
      var characterSet = this.target.ownerDocument.characterSet;
      const textToSubURI = Cc["@mozilla.org/intl/texttosuburi;1"].
                           getService(Ci.nsITextToSubURI);
      addresses = textToSubURI.unEscapeURIForUI(characterSet, addresses);
    }
    catch(ex) {
      // Do nothing.
    }

    var clipboard = Cc["@mozilla.org/widget/clipboardhelper;1"].
                    getService(Ci.nsIClipboardHelper);
    clipboard.copyString(addresses);
  },

  ///////////////
  // Utilities //
  ///////////////

  // Show/hide one item (specified via name or the item element itself).
  showItem: function(aItemOrId, aShow) {
    var item = aItemOrId.constructor == String ?
      document.getElementById(aItemOrId) : aItemOrId;
    if (item)
      item.hidden = !aShow;
  },

  // Set given attribute of specified context-menu item.  If the
  // value is null, then it removes the attribute (which works
  // nicely for the disabled attribute).
  setItemAttr: function(aID, aAttr, aVal ) {
    var elem = document.getElementById(aID);
    if (elem) {
      if (aVal == null) {
        // null indicates attr should be removed.
        elem.removeAttribute(aAttr);
      }
      else {
        // Set attr=val.
        elem.setAttribute(aAttr, aVal);
      }
    }
  },

  // Set context menu attribute according to like attribute of another node
  // (such as a broadcaster).
  setItemAttrFromNode: function(aItem_id, aAttr, aOther_id) {
    var elem = document.getElementById(aOther_id);
    if (elem && elem.getAttribute(aAttr) == "true")
      this.setItemAttr(aItem_id, aAttr, "true");
    else
      this.setItemAttr(aItem_id, aAttr, null);
  },

  // Temporary workaround for DOM api not yet implemented by XUL nodes.
  cloneNode: function(aItem) {
    // Create another element like the one we're cloning.
    var node = document.createElement(aItem.tagName);

    // Copy attributes from argument item to the new one.
    var attrs = aItem.attributes;
    for (var i = 0; i < attrs.length; i++) {
      var attr = attrs.item(i);
      node.setAttribute(attr.nodeName, attr.nodeValue);
    }

    // Voila!
    return node;
  },

  // Generate fully qualified URL for clicked-on link.
  getLinkURL: function() {
    var href = this.link.href;  
    if (href)
      return href;

    href = this.link.getAttributeNS("http://www.w3.org/1999/xlink",
                                    "href");

    if (!href || !href.match(/\S/)) {
      // Without this we try to save as the current doc,
      // for example, HTML case also throws if empty
      throw "Empty href";
    }

    return makeURLAbsolute(this.link.baseURI, href);
  },

  getLinkURI: function() {
    try {
      return makeURI(this.linkURL);
    }
    catch (ex) {
     // e.g. empty URL string
    }

    return null;
  },

  getLinkProtocol: function() {
    if (this.linkURI)
      return this.linkURI.scheme; // can be |undefined|

    return null;
  },

  // Get text of link.
  linkText: function() {
    var text = gatherTextUnder(this.link);
    if (!text || !text.match(/\S/)) {
      text = this.link.getAttribute("title");
      if (!text || !text.match(/\S/)) {
        text = this.link.getAttribute("alt");
        if (!text || !text.match(/\S/))
          text = this.linkURL;
      }
    }

    return text;
  },

  // Get selected text. Only display the first 15 chars.
  isTextSelection: function() {
    // Get 16 characters, so that we can trim the selection if it's greater
    // than 15 chars
    var selectedText = getBrowserSelection(16);

    if (!selectedText)
      return false;

    if (selectedText.length > 15)
      selectedText = selectedText.substr(0,15) + this.ellipsis;

    // Use the current engine if the search bar is visible, the default
    // engine otherwise.
    var engineName = "";
    var ss = Cc["@mozilla.org/browser/search-service;1"].
             getService(Ci.nsIBrowserSearchService);
    if (isElementVisible(BrowserSearch.searchBar))
      engineName = ss.currentEngine.name;
    else
      engineName = ss.defaultEngine.name;

    // format "Search <engine> for <selection>" string to show in menu
    var menuLabel = gNavigatorBundle.getFormattedString("contextMenuSearchText",
                                                        [engineName,
                                                         selectedText]);
    document.getElementById("context-searchselect").label = menuLabel;
    document.getElementById("context-searchselect").accessKey =
             gNavigatorBundle.getString("contextMenuSearchText.accesskey"); 

    return true;
  },

  // Returns true if anything is selected.
  isContentSelection: function() {
    return !document.commandDispatcher.focusedWindow.getSelection().isCollapsed;
  },

  toString: function () {
    return "contextMenu.target     = " + this.target + "\n" +
           "contextMenu.onImage    = " + this.onImage + "\n" +
           "contextMenu.onLink     = " + this.onLink + "\n" +
           "contextMenu.link       = " + this.link + "\n" +
           "contextMenu.inFrame    = " + this.inFrame + "\n" +
           "contextMenu.hasBGImage = " + this.hasBGImage + "\n";
  },

  // Returns true if aNode is a from control (except text boxes and images).
  // This is used to disable the context menu for form controls.
  isTargetAFormControl: function(aNode) {
    if (aNode instanceof HTMLInputElement)
      return (!aNode.mozIsTextField(false) && aNode.type != "image");

    return (aNode instanceof HTMLButtonElement) ||
           (aNode instanceof HTMLSelectElement) ||
           (aNode instanceof HTMLOptionElement) ||
           (aNode instanceof HTMLOptGroupElement);
  },

  isTargetATextBox: function(node) {
    if (node instanceof HTMLInputElement)
      return node.mozIsTextField(false);

    return (node instanceof HTMLTextAreaElement);
  },

  isTargetAKeywordField: function(aNode) {
    if (!(aNode instanceof HTMLInputElement))
      return false;

    var form = aNode.form;
    if (!form || aNode.type == "password")
      return false;

    var method = form.method.toUpperCase();

    // These are the following types of forms we can create keywords for:
    //
    // method   encoding type       can create keyword
    // GET      *                                 YES
    //          *                                 YES
    // POST                                       YES
    // POST     application/x-www-form-urlencoded YES
    // POST     text/plain                        NO (a little tricky to do)
    // POST     multipart/form-data               NO
    // POST     everything else                   YES
    return (method == "GET" || method == "") ||
           (form.enctype != "text/plain") && (form.enctype != "multipart/form-data");
  },

  // Determines whether or not the separator with the specified ID should be
  // shown or not by determining if there are any non-hidden items between it
  // and the previous separator.
  shouldShowSeparator: function (aSeparatorID) {
    var separator = document.getElementById(aSeparatorID);
    if (separator) {
      var sibling = separator.previousSibling;
      while (sibling && sibling.localName != "menuseparator") {
        if (!sibling.hidden)
          return true;
        sibling = sibling.previousSibling;
      }
    }
    return false;
  },

  addDictionaries: function() {
    var uri = formatURL("browser.dictionaries.download.url", true);

    var locale = "-";
    try {
      locale = gPrefService.getComplexValue("intl.accept_languages",
                                            Ci.nsIPrefLocalizedString).data;
    }
    catch (e) { }

    var version = "-";
    try {
      version = Cc["@mozilla.org/xre/app-info;1"].
                getService(Ci.nsIXULAppInfo).version;
    }
    catch (e) { }

    uri = uri.replace(/%LOCALE%/, escape(locale)).replace(/%VERSION%/, version);

    var newWindowPref = gPrefService.getIntPref("browser.link.open_newwindow");
    var where = newWindowPref == 3 ? "tab" : "window";

    openUILinkIn(uri, where);
  },

  bookmarkThisPage: function CM_bookmarkThisPage() {
    window.top.PlacesCommandHook.bookmarkPage(this.browser, PlacesUtils.bookmarksMenuFolderId, true);
  },

  bookmarkLink: function CM_bookmarkLink() {
    window.top.PlacesCommandHook.bookmarkLink(PlacesUtils.bookmarksMenuFolderId, this.linkURL,
                                              this.linkText());
  },

  addBookmarkForFrame: function CM_addBookmarkForFrame() {
    var doc = this.target.ownerDocument;
    var uri = doc.documentURIObject;

    var itemId = PlacesUtils.getMostRecentBookmarkForURI(uri);
    if (itemId == -1) {
      var title = doc.title;
      var description = PlacesUIUtils.getDescriptionFromDocument(doc);
      PlacesUIUtils.showMinimalAddBookmarkUI(uri, title, description);
    }
    else
      PlacesUIUtils.showItemProperties(itemId,
                                       PlacesUtils.bookmarks.TYPE_BOOKMARK);
  },

  savePageAs: function CM_savePageAs() {
    saveDocument(this.browser.contentDocument);
  },

  sendPage: function CM_sendPage() {
    MailIntegration.sendLinkForWindow(this.browser.contentWindow);  
  },

  printFrame: function CM_printFrame() {
    PrintUtils.print(this.target.ownerDocument.defaultView);
  },

  switchPageDirection: function CM_switchPageDirection() {
    SwitchDocumentDirection(this.browser.contentWindow);
  },

  mediaCommand : function CM_mediaCommand(command) {
    var media = this.target;

    switch (command) {
      case "play":
        media.play();
        break;
      case "pause":
        media.pause();
        break;
      case "mute":
        media.muted = true;
        break;
      case "unmute":
        media.muted = false;
        break;
      case "hidecontrols":
        media.removeAttribute("controls");
        break;
      case "showcontrols":
        media.setAttribute("controls", "true");
        break;
    }
  },

  copyMediaLocation : function () {
    var clipboard = Cc["@mozilla.org/widget/clipboardhelper;1"].
                    getService(Ci.nsIClipboardHelper);
    clipboard.copyString(this.mediaURL);
  },

  get imageURL() {
    if (this.onImage)
      return this.mediaURL;
    return "";
  }
};
