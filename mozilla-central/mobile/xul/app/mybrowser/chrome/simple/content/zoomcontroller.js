
function ZoomController(aBrowser) {
    this._browser = aBrowser;
};

// ZoomControler sets browser zoom
ZoomController.prototype = {
	_minScale : 0.5,
	_maxScale : 3,
    _target : null,

	set scale(s)
	{
		var clamp = Math.min(this._maxScale, Math.max(this._minScale, s));
		clamp = Math.floor(clamp * 1000) / 1000;	// Round to 3 digits
		if (clamp == this._browser.markupDocumentViewer.fullZoom)
			return;

		this._browser.markupDocumentViewer.fullZoom = clamp;

		// If we've zoomed out of the viewport, scroll us back in
		var leftEdge = this._browser.contentWindow.scrollX + this._browser.contentWindow.document.documentElement.clientWidth;
		var scrollX = this._browser.contentWindow.document.documentElement.scrollWidth - leftEdge;
		if (scrollX < 0)
			this._browser.contentWindow.scrollBy(scrollX, 0);
	},

	get scale()
	{
		return this._browser.markupDocumentViewer.fullZoom;
	},

	reset: function()
	{
		this._minScale = ZoomController.prototype._minScale;
		this._maxScale = ZoomController.prototype._maxScale;
	},

	fitContent: function()
	{
        this._target = null;
		try {
			var oldScale = this.scale;
			this.scale = 1;		// reset the scale to 1 forces document to preferred size
			var body = this._browser.contentWindow.document.body;
			var html = this._browser.contentWindow.document.documentElement;
			var newScale = this.scale;
			var finalWidth = html.clientWidth;
		}
		catch(e) {
			dump(e + "\n");
			return;
		}

		var prefScrollWidth = Math.max(html.scrollWidth, body.scrollWidth); // empirical hack, no idea why
		if (prefScrollWidth > (this._browser.boxObject.width - 10) )	{
			// body wider than window, scale id down
			// we substract 10 to compensate for 10 pixel browser left margin
			newScale = (this._browser.boxObject.width ) / prefScrollWidth;
			finalWidth = prefScrollWidth;
		}
		body.style.minWidth = body.style.maxWidth = (finalWidth -20) + "px";
		this._minScale = Math.max(this._minScale, newScale);
        this.scale = newScale;
	},

	getPagePosition: function (el)
	{
		var r = el.getBoundingClientRect();
		retVal = {
            width: r.right - r.left,
            height: r.bottom - r.top,
			x: r.left + this._browser.contentWindow.scrollX,
            y: r.top + this._browser.contentWindow.scrollY
        };
		return retVal;
	},

	getWindowRect: function()
	{
		return {
            x: this._browser.contentWindow.scrollX,
			y: this._browser.contentWindow.scrollY,
			width: this._browser.boxObject.width / this.scale,
			height: this._browser.boxObject.height / this.scale
        };
	},

	toggleZoom: function(el)
	{
		if (!el) return;

        if (this.scale == 1 || el != this._target) {
			this.zoomToElement(el);
            this._target = el;
        }
        else {
			this.scale = 1;
            this._target = null;
        }
	},

	zoomToElement: function(el)
	{
        var margin = 8;

        // First get the width of the element so we can scale to its width
		var elRect = this.getPagePosition(el);
        this.scale = (this._browser.boxObject.width) / (elRect.width + 2 * margin);

        // Now that we are scaled, we need to scroll to the element
        elRect = this.getPagePosition(el);
        winRect = this.getWindowRect();
        this._browser.contentWindow.scrollTo(Math.max(elRect.x - margin, 0), Math.max(0, elRect.y - margin));
	}
};

