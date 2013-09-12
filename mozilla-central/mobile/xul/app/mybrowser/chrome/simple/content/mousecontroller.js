var MouseController = function (browser) {
    this.init(browser);
}

MouseController.prototype = {
        _browser: null,
        _contextID : null,

        init: function(aBrowser)
        {
            this._browser = aBrowser;
            this._browser.addEventListener("mousedown", this, true);
            this._browser.addEventListener("mouseup",this, true);
            dump("init\n");
        },

        handleEvent: function(e)
        {
            dump("handleEvent\n");
            if (! e.type in this)
                dump("MouseController called with unknown event type " + e.type + "\n");
            this[e.type](e);
        },

        mousedown: function(aEvent)
        {
            dump("mousedown\n");
            // I am thinking that this list is going to grow
            if (aEvent.target instanceof HTMLInputElement ||
                aEvent.target instanceof HTMLSelectElement )
                return;

            // Check to see if we should treat this as a double-click
            if (this.firstEvent &&
                (aEvent.timeStamp - this.firstEvent.timeStamp) < 400 &&
                Math.abs(aEvent.clientX - this.firstEvent.clientX) < 30 &&
                Math.abs(aEvent.clientY - this.firstEvent.clientY) < 30) {
                this.dblclick(aEvent);
                return;
            }

            var isTextField = aEvent.target instanceof HTMLTextAreaElement;
            if (aEvent.target instanceof HTMLInputElement &&
                (aEvent.target.type == "text" || aEvent.target.type == "password"))
                isTextField = true;

            this.lastEvent = this.firstEvent = aEvent;
            this.fingerDistance = 100;
            if (!isTextField) {
                this.mousemove = aEvent.button != 2 ? this.mousePan : this.mouseZoom;
                this._browser.addEventListener("mousemove", this, true);
            }

            var self = this;
            this._contextID = setTimeout(function() { self.contextMenu(aEvent); }, 900);

            //FIX Show scrollbars now

            aEvent.stopPropagation();
            aEvent.preventDefault();
            dump("mousedown2\n");
        },

        mouseup: function(aEvent)
        {

            this._browser.removeEventListener("mousemove", this, true);
            if (this._contextID) {
                clearTimeout(this._contextID);
                this._contextID = null;
            }

            // I am thinking that this list is going to grow
            if (aEvent.target instanceof HTMLInputElement ||
                aEvent.target instanceof HTMLSelectElement )
                return;

            //FIX Hide scrollbars now

            // Cancel link clicks if we've been dragging for a while
            var totalDistance = Math.sqrt(
                                          Math.pow(this.firstEvent.clientX - aEvent.clientX, 2) +
                                          Math.pow(this.firstEvent.clientY - aEvent.clientY, 2));
            if (totalDistance > 10)
                aEvent.preventDefault();

            // Keep scrolling if there is enough momentum
            /*
              if (this.lastEvent && "momentum" in this.lastEvent && this.mousemove == this.mousePan)
              {
              var speedX = this.lastEvent.momentum.x / this.lastEvent.momentum.time;
              var speedY = this.lastEvent.momentum.y / this.lastEvent.momentum.time;
              const speedLimit = 6;
              if (Math.abs(speedY) > speedLimit)
              speedY = speedY > 0 ? speedLimit : -speedLimit;
              if (Math.abs(speedX) > speedLimit)
              speedX = speedX > 0 ? speedLimit : -speedLimit;

              var lastCall = Date.now();
              browser = this._browser;
              const speedFactor = 0.1;
              const decayFactor = 0.9995;
              const cutoff = 0.2;
              var intervalId = setInterval( function() {
              var elapsed = (Date.now() - lastCall);
              var x = elapsed * speedX * speedFactor, y = elapsed * speedY * speedFactor;
              browser.contentWindow.scrollBy(-x, -y);
              var slowdown = Math.pow(decayFactor, elapsed);
              speedX *= slowdown;
              speedY *= slowdown;
              if (Math.abs(speedX) < cutoff && Math.abs(speedY) < cutoff)
              clearInterval(intervalId);
              }, 0);
              }*/
            dump("mouseup1\n");
            var fl = this._browser.QueryInterface(Components.interfaces.nsIFrameLoaderOwner).frameLoader;
            var sx = fl.viewportScrollX;
            var sy = fl.viewportScrollY;
            fl.messageManager.sendAsyncMessage("Content:SetCacheViewport", {
              x: sx,
              y: sy,
              w: 768,
              h: 1024,
              zoomLevel: 1
            });
            dump("mouseup2\n");
        },

        mouseZoom: function(e)
        {
            var deltaX = e.screenX - this.firstEvent.screenX + 100;
            var deltaY = e.screenY - this.firstEvent.screenY;
            var newDist = Math.sqrt(Math.pow(deltaX, 2) + Math.pow(deltaY, 2));
            var scale = newDist / this.fingerDistance;
            if (e.screenX < this.firstEvent.screenX && scale > 1)
                scale = 1 / scale;
            var newZoom = scale * this._browser.markupDocumentViewer.fullZoom;
            this.fingerDistance = Math.max(0.1, newDist);
            this._browser.zoomController.scale = newZoom;
            this.lastEvent = e;

            //FIX Adjust scrollbars now
            e.stopPropagation();
            e.preventDefault();
        },

        mousePan: function(aEvent)
        {
            var x = aEvent.clientX - this.lastEvent.clientX;
            var y = aEvent.clientY - this.lastEvent.clientY;
            if (Math.abs(x) < 5 && Math.abs(y) < 5)
                return;

            if (this._contextID) {
                clearTimeout(this._contextID);
                this._contextID = null;
            }

            if (this.lastEvent) {
                aEvent.momentum = {
                    time: Math.max(aEvent.timeStamp - this.lastEvent.timeStamp, 1),
                    x: x,
                    y: y
                };
            }
            
            //this._browser.contentWindow.scrollBy(-x, -y); //remote == false;
            var fl = this._browser.QueryInterface(Components.interfaces.nsIFrameLoaderOwner).frameLoader;
            fl.scrollViewportBy(-x, -y);
            
            this.lastEvent = aEvent;

            //FIX Adjust scrollbars now

            aEvent.stopPropagation();
            aEvent.preventDefault();
        },

        dblclick: function(aEvent)
        {
            // Find the target by walking the dom. We want to zoom in on the block elements
            var target = aEvent.target;
            aEvent.preventDefault();
            while (target && target.nodeName != "HTML") {
                var disp = window.getComputedStyle(target, "").getPropertyValue("display");
                if (!disp.match(/(inline)/g)) {
                    this._browser.zoomController.toggleZoom(target);
                    break;
                }
                else {
                    target = target.parentNode;
                }
            }
            aEvent.stopPropagation();
            aEvent.preventDefault();
        },

        contextMenu: function(aEvent)
        {
            if (this._contextID && this._browser.contextMenu) {
                var popup = document.getElementById(this._browser.contextMenu);
                popup.openPopup(this._browser, "", aEvent.clientX, aEvent.clientY, true, false);

                this._browser.removeEventListener("mousemove", this, true);
                this._contextID = null;

                aEvent.stopPropagation();
                aEvent.preventDefault();
            }
        },

        drag : function(aEvent){
            aEvent.stopPropagation();
            aEvent.preventDefault();
            return true;
        },

        dragstart : function(aEvent){
            return this.drag(aEvent);
        },

        draggesture : function(aEvent){
            return this.drag(aEvent);
        }
}


