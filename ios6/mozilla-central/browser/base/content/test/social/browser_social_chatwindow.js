/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

function test() {
  waitForExplicitFinish();

  let manifest = { // normal provider
    name: "provider 1",
    origin: "https://example.com",
    sidebarURL: "https://example.com/browser/browser/base/content/test/social/social_sidebar.html",
    workerURL: "https://example.com/browser/browser/base/content/test/social/social_worker.js",
    iconURL: "https://example.com/browser/browser/base/content/test/moz.png"
  };
  let oldwidth = window.outerWidth; // we futz with this, so we restore it
  runSocialTestWithProvider(manifest, function (finishcb) {
    runSocialTests(tests, undefined, undefined, function () {
      let chats = document.getElementById("pinnedchats");
      ok(chats.children.length == 0, "no chatty children left behind");
      window.resizeTo(oldwidth, window.outerHeight);
      finishcb();
    });
  });
}

var tests = {
  testOpenCloseChat: function(next) {
    let chats = document.getElementById("pinnedchats");
    let port = Social.provider.getWorkerPort();
    ok(port, "provider has a port");
    port.onmessage = function (e) {
      let topic = e.data.topic;
      switch (topic) {
        case "got-sidebar-message":
          port.postMessage({topic: "test-chatbox-open"});
          break;
        case "got-chatbox-visibility":
          if (e.data.result == "hidden") {
            ok(true, "chatbox got minimized");
            chats.selectedChat.toggle();
          } else if (e.data.result == "shown") {
            ok(true, "chatbox got shown");
            // close it now
            let iframe = chats.selectedChat.iframe;
            iframe.addEventListener("unload", function chatUnload() {
              iframe.removeEventListener("unload", chatUnload, true);
              ok(true, "got chatbox unload on close");
              port.close();
              next();
            }, true);
            chats.selectedChat.close();
          }
          break;
        case "got-chatbox-message":
          ok(true, "got chatbox message");
          ok(e.data.result == "ok", "got chatbox windowRef result: "+e.data.result);
          chats.selectedChat.toggle();
          break;
      }
    }
    port.postMessage({topic: "test-init", data: { id: 1 }});
  },
  testOpenMinimized: function(next) {
    // In this case the sidebar opens a chat (without specifying minimized).
    // We then minimize it and have the sidebar reopen the chat (again without
    // minimized).  On that second call the chat should open and no longer
    // be minimized.
    let chats = document.getElementById("pinnedchats");
    let port = Social.provider.getWorkerPort();
    let seen_opened = false;
    port.onmessage = function (e) {
      let topic = e.data.topic;
      switch (topic) {
        case "test-init-done":
          port.postMessage({topic: "test-chatbox-open"});
          break;
        case "chatbox-opened":
          is(e.data.result, "ok", "the sidebar says it got a chatbox");
          if (!seen_opened) {
            // first time we got the opened message, so minimize the chat then
            // re-request the same chat to be opened - we should get the
            // message again and the chat should be restored.
            ok(!chats.selectedChat.minimized, "chat not initially minimized")
            chats.selectedChat.minimized = true
            seen_opened = true;
            port.postMessage({topic: "test-chatbox-open"});
          } else {
            // This is the second time we've seen this message - there should
            // be exactly 1 chat open and it should no longer be minimized.
            let chats = document.getElementById("pinnedchats");
            ok(!chats.selectedChat.minimized, "chat no longer minimized")
            chats.selectedChat.close();
            is(chats.selectedChat, null, "should only have been one chat open");
            port.close();
            next();
          }
      }
    }
    port.postMessage({topic: "test-init", data: { id: 1 }});
  },
  // In this case the *worker* opens a chat (so minimized is specified).
  // The worker then makes the same call again - as that second call also
  // specifies "minimized" the chat should *not* be restored.
  testWorkerChatWindowMinimized: function(next) {
    const chatUrl = "https://example.com/browser/browser/base/content/test/social/social_chat.html";
    let port = Social.provider.getWorkerPort();
    let seen_opened = false;
    ok(port, "provider has a port");
    port.postMessage({topic: "test-init"});
    port.onmessage = function (e) {
      let topic = e.data.topic;
      switch (topic) {
        case "got-chatbox-message":
          ok(true, "got a chat window opened");
          let chats = document.getElementById("pinnedchats");
          if (!seen_opened) {
            // first time we got the opened message, so minimize the chat then
            // re-request the same chat to be opened - we should get the
            // message again and the chat should be restored.
            ok(chats.selectedChat.minimized, "chatbox from worker opened as minimized");
            seen_opened = true;
            port.postMessage({topic: "test-worker-chat", data: chatUrl});
            // Sadly there is no notification we can use to know the chat was
            // re-opened :(  So we ask the chat window to "ping" us - by then
            // the second request should have made it.
            chats.selectedChat.iframe.contentWindow.wrappedJSObject.pingWorker();
          } else {
            // This is the second time we've seen this message - there should
            // be exactly 1 chat open and it should still be minimized.
            let chats = document.getElementById("pinnedchats");
            ok(chats.selectedChat.minimized, "chat still minimized")
            chats.selectedChat.close();
            is(chats.selectedChat, null, "should only have been one chat open");
            port.close();
            next();
          }
          break;
      }
    }
    port.postMessage({topic: "test-worker-chat", data: chatUrl});
  },
  testManyChats: function(next) {
    // open enough chats to overflow the window, then check
    // if the menupopup is visible
    let port = Social.provider.getWorkerPort();
    ok(port, "provider has a port");
    port.postMessage({topic: "test-init"});
    let width = document.documentElement.boxObject.width;
    let numToOpen = (width / 200) + 1;
    port.onmessage = function (e) {
      let topic = e.data.topic;
      switch (topic) {
        case "got-chatbox-message":
          numToOpen--;
          if (numToOpen >= 0) {
            // we're waiting for all to open
            ok(true, "got a chat window opened");
            break;
          }
          // close our chats now
          let chats = document.getElementById("pinnedchats");
          ok(!chats.menupopup.parentNode.collapsed, "menu selection is visible");
          while (chats.selectedChat) {
            chats.selectedChat.close();
          }
          ok(!chats.selectedChat, "chats are all closed");
          port.close();
          next();
          break;
      }
    }
    let num = numToOpen;
    while (num-- > 0) {
      port.postMessage({topic: "test-chatbox-open", data: { id: num }});
    }
  },
  testWorkerChatWindow: function(next) {
    const chatUrl = "https://example.com/browser/browser/base/content/test/social/social_chat.html";
    let port = Social.provider.getWorkerPort();
    ok(port, "provider has a port");
    port.postMessage({topic: "test-init"});
    port.onmessage = function (e) {
      let topic = e.data.topic;
      switch (topic) {
        case "got-chatbox-message":
          ok(true, "got a chat window opened");
          let chats = document.getElementById("pinnedchats");
          ok(chats.selectedChat.minimized, "chatbox from worker opened as minimized");
          while (chats.selectedChat) {
            chats.selectedChat.close();
          }
          ok(!chats.selectedChat, "chats are all closed");
          ensureSocialUrlNotRemembered(chatUrl);
          port.close();
          next();
          break;
      }
    }
    port.postMessage({topic: "test-worker-chat", data: chatUrl});
  },
  testCloseSelf: function(next) {
    let chats = document.getElementById("pinnedchats");
    let port = Social.provider.getWorkerPort();
    ok(port, "provider has a port");
    port.onmessage = function (e) {
      let topic = e.data.topic;
      switch (topic) {
        case "test-init-done":
          port.postMessage({topic: "test-chatbox-open"});
          break;
        case "got-chatbox-visibility":
          is(e.data.result, "shown", "chatbox shown");
          port.close(); // don't want any more visibility messages.
          let chat = chats.selectedChat;
          ok(chat.parentNode, "chat has a parent node before it is closed");
          // ask it to close itself.
          let doc = chat.iframe.contentDocument;
          let evt = doc.createEvent("CustomEvent");
          evt.initCustomEvent("socialTest-CloseSelf", true, true, {});
          doc.documentElement.dispatchEvent(evt);
          ok(!chat.parentNode, "chat is now closed");
          port.close();
          next();
          break;
      }
    }
    port.postMessage({topic: "test-init", data: { id: 1 }});
  },
  testSameChatCallbacks: function(next) {
    let chats = document.getElementById("pinnedchats");
    let port = Social.provider.getWorkerPort();
    let seen_opened = false;
    port.onmessage = function (e) {
      let topic = e.data.topic;
      switch (topic) {
        case "test-init-done":
          port.postMessage({topic: "test-chatbox-open"});
          break;
        case "chatbox-opened":
          is(e.data.result, "ok", "the sidebar says it got a chatbox");
          if (seen_opened) {
            // This is the second time we've seen this message - there should
            // be exactly 1 chat open.
            let chats = document.getElementById("pinnedchats");
            chats.selectedChat.close();
            is(chats.selectedChat, null, "should only have been one chat open");
            port.close();
            next();
          } else {
            // first time we got the opened message, so re-request the same
            // chat to be opened - we should get the message again.
            seen_opened = true;
            port.postMessage({topic: "test-chatbox-open"});
          }
      }
    }
    port.postMessage({topic: "test-init", data: { id: 1 }});
  },

  // check removeAll does the right thing
  testRemoveAll: function(next, mode) {
    let port = Social.provider.getWorkerPort();
    port.postMessage({topic: "test-init"});
    get3ChatsForCollapsing(mode || "normal", function() {
      let chatbar = window.SocialChatBar.chatbar;
      chatbar.removeAll();
      // should be no evidence of any chats left.
      is(chatbar.childNodes.length, 0, "should be no chats left");
      checkPopup();
      is(chatbar.selectedChat, null, "nothing should be selected");
      is(chatbar.chatboxForURL.size, 0, "chatboxForURL map should be empty");
      port.close();
      next();
    });
  },

  testRemoveAllMinimized: function(next) {
    this.testRemoveAll(next, "minimized");
  },

  // resize and collapse testing.
  testBrowserResize: function(next, mode) {
    let chats = document.getElementById("pinnedchats");
    let port = Social.provider.getWorkerPort();
    port.postMessage({topic: "test-init"});
    get3ChatsForCollapsing(mode || "normal", function(first, second, third) {
      let chatWidth = chats.getTotalChildWidth(first);
      ok(chatWidth, "have a chatwidth");
      let popupWidth = getPopupWidth();
      ok(popupWidth, "have a popupwidth");
      info("starting resize tests - each chat's width is " + chatWidth +
           " and the popup width is " + popupWidth);
      resizeAndCheckWidths(first, second, third, [
        [chatWidth-1, false, false, true, "to < 1 chat width - only last should be visible."],
        [chatWidth+1, false, false, true, "one pixel more then one fully exposed (not counting popup) - still only 1."],
        [chatWidth+popupWidth+1, false, false, true, "one pixel more than one fully exposed (including popup) - still only 1."],
        [chatWidth*2-1, false, false, true, "second not showing by 1 pixel (not counting popup) - only 1 exposed."],
        [chatWidth*2+popupWidth-1, false, false, true, "second not showing by 1 pixel (including popup) - only 1 exposed."],
        [chatWidth*2+popupWidth+1, false, true, true, "big enough to fit 2 - nub remains visible as first is still hidden"],
        [chatWidth*3+popupWidth-1, false, true, true, "one smaller than the size necessary to display all three - first still hidden"],
        [chatWidth*3+popupWidth+1, true, true, true, "big enough to fit all - all exposed (which removes the nub)"],
        [chatWidth*3, true, true, true, "now the nub is hidden we can resize back down to chatWidth*3 before overflow."],
        [chatWidth*3-1, false, true, true, "one pixel less and the first is again collapsed (and the nub re-appears)"],
        [chatWidth*2+popupWidth+1, false, true, true, "back down to just big enough to fit 2"],
        [chatWidth*2+popupWidth-1, false, false, true, "back down to just not enough to fit 2"],
        [chatWidth*3+popupWidth+1, true, true, true, "now a large jump to make all 3 visible (ie, affects 2)"],
        [chatWidth*1.5, false, false, true, "and a large jump back down to 1 visible (ie, affects 2)"],
      ], function() {
        closeAllChats();
        port.close();
        next();
      });
    });
  },

  testBrowserResizeMinimized: function(next) {
    this.testBrowserResize(next, "minimized");
  },

  testShowWhenCollapsed: function(next) {
    let port = Social.provider.getWorkerPort();
    port.postMessage({topic: "test-init"});
    get3ChatsForCollapsing("normal", function(first, second, third) {
      let chatbar = window.SocialChatBar.chatbar;
      chatbar.showChat(first);
      ok(!first.collapsed, "first should no longer be collapsed");
      ok(second.collapsed ||  third.collapsed, false, "one of the others should be collapsed");
      closeAllChats();
      port.close();
      next();
    });
  },

  testActivity: function(next) {
    let port = Social.provider.getWorkerPort();
    port.postMessage({topic: "test-init"});
    get3ChatsForCollapsing("normal", function(first, second, third) {
      let chatbar = window.SocialChatBar.chatbar;
      is(chatbar.selectedChat, third, "third chat should be selected");
      ok(!chatbar.selectedChat.hasAttribute("activity"), "third chat should have no activity");
      // send an activity message to the second.
      ok(!second.hasAttribute("activity"), "second chat should have no activity");
      let iframe2 = second.iframe;
      let evt = iframe2.contentDocument.createEvent("CustomEvent");
      evt.initCustomEvent("socialChatActivity", true, true, {});
      iframe2.contentDocument.documentElement.dispatchEvent(evt);
      // second should have activity.
      ok(second.hasAttribute("activity"), "second chat should now have activity");
      // select the second - it should lose "activity"
      chatbar.selectedChat = second;
      ok(!second.hasAttribute("activity"), "second chat should no longer have activity");
      // Now try the first - it is collapsed, so the 'nub' also gets activity attr.
      ok(!first.hasAttribute("activity"), "first chat should have no activity");
      let iframe1 = first.iframe;
      let evt = iframe1.contentDocument.createEvent("CustomEvent");
      evt.initCustomEvent("socialChatActivity", true, true, {});
      iframe1.contentDocument.documentElement.dispatchEvent(evt);
      ok(first.hasAttribute("activity"), "first chat should now have activity");
      ok(chatbar.nub.hasAttribute("activity"), "nub should also have activity");
      // first is collapsed, so use openChat to get it.
      chatbar.openChat(Social.provider, first.getAttribute("src"));
      ok(!first.hasAttribute("activity"), "first chat should no longer have activity");
      // The nub should lose the activity flag here too
      todo(!chatbar.nub.hasAttribute("activity"), "Bug 806266 - nub should no longer have activity");
      // TODO: tests for bug 806266 should arrange to have 2 chats collapsed
      // then open them checking the nub is updated correctly.
      // Now we will go and change the embedded iframe in the second chat and
      // ensure the activity magic still works (ie, check that the unload for
      // the iframe didn't cause our event handlers to be removed.)
      ok(!second.hasAttribute("activity"), "second chat should have no activity");
      let subiframe = iframe2.contentDocument.getElementById("iframe");
      subiframe.contentWindow.addEventListener("unload", function subunload() {
        subiframe.contentWindow.removeEventListener("unload", subunload);
        // ensure all other unload listeners have fired.
        executeSoon(function() {
          let evt = iframe2.contentDocument.createEvent("CustomEvent");
          evt.initCustomEvent("socialChatActivity", true, true, {});
          iframe2.contentDocument.documentElement.dispatchEvent(evt);
          ok(second.hasAttribute("activity"), "second chat still has activity after unloading sub-iframe");
          closeAllChats();
          port.close();
          next();
        })
      })
      subiframe.setAttribute("src", "data:text/plain:new location for iframe");
    });
  },

  testOnlyOneCallback: function(next) {
    let chats = document.getElementById("pinnedchats");
    let port = Social.provider.getWorkerPort();
    let numOpened = 0;
    port.onmessage = function (e) {
      let topic = e.data.topic;
      switch (topic) {
        case "test-init-done":
          port.postMessage({topic: "test-chatbox-open"});
          break;
        case "chatbox-opened":
          numOpened += 1;
          port.postMessage({topic: "ping"});
          break;
        case "pong":
          executeSoon(function() {
            is(numOpened, 1, "only got one open message");
            chats.removeAll();
            port.close();
            next();
          });
      }
    }
    port.postMessage({topic: "test-init", data: { id: 1 }});
  },

  testSecondTopLevelWindow: function(next) {
    // Bug 817782 - check chats work in new top-level windows.
    const chatUrl = "https://example.com/browser/browser/base/content/test/social/social_chat.html";
    let port = Social.provider.getWorkerPort();
    let secondWindow;
    port.onmessage = function(e) {
      if (e.data.topic == "test-init-done") {
        secondWindow = OpenBrowserWindow();
        secondWindow.addEventListener("load", function loadListener() {
          secondWindow.removeEventListener("load", loadListener);
          port.postMessage({topic: "test-worker-chat", data: chatUrl});
        });
      } else if (e.data.topic == "got-chatbox-message") {
        // the chat was created - let's make sure it was created in the second window.
        is(secondWindow.SocialChatBar.chatbar.childElementCount, 1);
        secondWindow.close();
        next();
      }
    }
    port.postMessage({topic: "test-init"});
  },

  testChatWindowChooser: function(next) {
    // Tests that when a worker creates a chat, it is opened in the correct
    // window.
    const chatUrl = "https://example.com/browser/browser/base/content/test/social/social_chat.html";
    let chatId = 1;
    let port = Social.provider.getWorkerPort();
    port.postMessage({topic: "test-init"});

    function openChat(callback) {
      port.onmessage = function(e) {
        if (e.data.topic == "got-chatbox-message")
          callback();
      }
      let url = chatUrl + "?" + (chatId++);
      port.postMessage({topic: "test-worker-chat", data: url});
    }

    // open a chat (it will open in the main window)
    ok(!window.SocialChatBar.hasChats, "first window should start with no chats");
    openChat(function() {
      ok(window.SocialChatBar.hasChats, "first window has the chat");
      // create a second window - although this will be the "most recent",
      // the fact the first window has a chat open means the first will be targetted.
      let secondWindow = OpenBrowserWindow();
      secondWindow.addEventListener("load", function loadListener() {
        secondWindow.removeEventListener("load", loadListener);
        ok(!secondWindow.SocialChatBar.hasChats, "second window has no chats");
        openChat(function() {
          ok(!secondWindow.SocialChatBar.hasChats, "second window still has no chats");
          is(window.SocialChatBar.chatbar.childElementCount, 2, "first window now has 2 chats");
          window.SocialChatBar.chatbar.removeAll();
          // now open another chat - it should open in the second window (as
          // it is the "most recent" and no other windows have chats)
          openChat(function() {
            ok(!window.SocialChatBar.hasChats, "first window has no chats");
            ok(secondWindow.SocialChatBar.hasChats, "second window has a chat");
            secondWindow.close();
            next();
          });
        });
      })
    });
  },

  // XXX - note this must be the last test until we restore the login state
  // between tests...
  testCloseOnLogout: function(next) {
    const chatUrl = "https://example.com/browser/browser/base/content/test/social/social_chat.html";
    let port = Social.provider.getWorkerPort();
    ok(port, "provider has a port");
    port.postMessage({topic: "test-init"});
    port.onmessage = function (e) {
      let topic = e.data.topic;
      switch (topic) {
        case "got-chatbox-message":
          ok(true, "got a chat window opened");
          port.postMessage({topic: "test-logout"});
          port.close();
          waitForCondition(function() document.getElementById("pinnedchats").firstChild == null,
                           next,
                           "chat windows didn't close");
          break;
      }
    }
    port.postMessage({topic: "test-worker-chat", data: chatUrl});
  },
}

// And lots of helpers for the resize tests.
function get3ChatsForCollapsing(mode, cb) {
  // We make one chat, then measure its size.  We then resize the browser to
  // ensure a second can be created fully visible but a third can not - then
  // create the other 2.  first will will be collapsed, second fully visible
  // and the third also visible and the "selected" one.
  // To make our life easier we don't go via the worker and ports so we get
  // more control over creation *and* to make the code much simpler.  We
  // assume the worker/port stuff is individually tested above.
  let chatbar = window.SocialChatBar.chatbar;
  let chatWidth = undefined;
  let num = 0;
  is(chatbar.childNodes.length, 0, "chatbar starting empty");
  is(chatbar.menupopup.childNodes.length, 0, "popup starting empty");

  makeChat(mode, "first chat", function() {
    // got the first one.
    checkPopup();
    ok(chatbar.menupopup.parentNode.collapsed, "menu selection isn't visible");
    // we kinda cheat here and get the width of the first chat, assuming
    // that all future chats will have the same width when open.
    chatWidth = chatbar.calcTotalWidthOf(chatbar.selectedChat);
    let desired = chatWidth * 2.5;
    resizeWindowToChatAreaWidth(desired, function(sizedOk) {
      ok(sizedOk, "can't do any tests without this width");
      checkPopup();
      makeChat(mode, "second chat", function() {
        is(chatbar.childNodes.length, 2, "now have 2 chats");
        checkPopup();
        // and create the third.
        makeChat(mode, "third chat", function() {
          is(chatbar.childNodes.length, 3, "now have 3 chats");
          checkPopup();
          // XXX - this is a hacky implementation detail around the order of
          // the chats.  Ideally things would be a little more sane wrt the
          // other in which the children were created.
          let second = chatbar.childNodes[2];
          let first = chatbar.childNodes[1];
          let third = chatbar.childNodes[0];
          ok(first.collapsed && !second.collapsed && !third.collapsed, "collapsed state as promised");
          is(chatbar.selectedChat, third, "third is selected as promised")
          info("have 3 chats for collapse testing - starting actual test...");
          cb(first, second, third);
        }, mode);
      }, mode);
    });
  }, mode);
}

function makeChat(mode, uniqueid, cb) {
  const chatUrl = "https://example.com/browser/browser/base/content/test/social/social_chat.html";
  let provider = Social.provider;
  window.SocialChatBar.openChat(provider, chatUrl + "?id=" + uniqueid, function(chat) {
    // we can't callback immediately or we might close the chat during
    // this event which upsets the implementation - it is only 1/2 way through
    // handling the load event.
    chat.document.title = uniqueid;
    executeSoon(cb);
  }, mode);
}

function checkPopup() {
  // popup only showing if any collapsed popup children.
  let chatbar = window.SocialChatBar.chatbar;
  let numCollapsed = 0;
  for (let chat of chatbar.childNodes) {
    if (chat.collapsed) {
      numCollapsed += 1;
      // and it have a menuitem weakmap
      is(chatbar.menuitemMap.get(chat).nodeName, "menuitem", "collapsed chat has a menu item");
    } else {
      ok(!chatbar.menuitemMap.has(chat), "open chat has no menu item");
    }
  }
  is(chatbar.menupopup.parentNode.collapsed, numCollapsed == 0, "popup matches child collapsed state");
  is(chatbar.menupopup.childNodes.length, numCollapsed, "popup has correct count of children");
  // todo - check each individual elt is what we expect?
}

// Resize the main window so the chat area's boxObject is |desired| wide.
// Does a callback passing |true| if the window is now big enough or false
// if we couldn't resize large enough to satisfy the test requirement.
function resizeWindowToChatAreaWidth(desired, cb) {
  let current = window.SocialChatBar.chatbar.getBoundingClientRect().width;
  let delta = desired - current;
  info("resizing window so chat area is " + desired + " wide, currently it is "
       + current + ".  Screen avail is " + window.screen.availWidth
       + ", current outer width is " + window.outerWidth);

  // WTF?  Some test boxes will resize to fractional values - eg: we
  // request 660px but actually get 659.5!?
  let widthDeltaCloseEnough = function(d) {
    return Math.abs(d) <= 0.5;
  }

  // attempting to resize by (0,0), unsurprisingly, doesn't cause a resize
  // event - so just callback saying all is well.
  if (widthDeltaCloseEnough(delta)) {
    cb(true);
    return;
  }
  // On lo-res screens we may already be maxed out but still smaller than the
  // requested size, so asking to resize up also will not cause a resize event.
  // So just callback now saying the test must be skipped.
  if (window.screen.availWidth - window.outerWidth < delta) {
    info("skipping this as screen available width is less than necessary");
    cb(false);
    return;
  }
  // Otherwise we request resize and expect a resize event
  window.addEventListener("resize", function resize_handler() {
    window.removeEventListener("resize", resize_handler);
    // we did resize - but did we get far enough to be able to continue?
    let newSize = window.SocialChatBar.chatbar.getBoundingClientRect().width;
    let sizedOk = widthDeltaCloseEnough(newSize - desired);
    if (!sizedOk) {
      // not an error...
      info("skipping this as we can't resize chat area to " + desired + " - got " + newSize);
    }
    cb(sizedOk);
  });
  window.resizeBy(delta, 0);
}

function resizeAndCheckWidths(first, second, third, checks, cb) {
  if (checks.length == 0) {
    cb(); // nothing more to check!
    return;
  }
  let [width, firstVisible, secondVisible, thirdVisible, why] = checks.shift();
  info("Check: " + why);
  info("resizing window to " + width + ", expect visibility of " + firstVisible + "/" + secondVisible + "/" + thirdVisible);  
  resizeWindowToChatAreaWidth(width, function(sizedOk) {
    checkPopup();
    if (sizedOk) {
      is(!first.collapsed, firstVisible, "first should be " + (firstVisible ? "visible" : "hidden"));
      is(!second.collapsed, secondVisible, "second should be " + (secondVisible ? "visible" : "hidden"));
      is(!third.collapsed, thirdVisible, "third should be " + (thirdVisible ? "visible" : "hidden"));
    }
    resizeAndCheckWidths(first, second, third, checks, cb);
  });
}

function getPopupWidth() {
  let popup = window.SocialChatBar.chatbar.menupopup;
  ok(!popup.parentNode.collapsed, "asking for popup width when it is visible");
  let cs = document.defaultView.getComputedStyle(popup.parentNode);
  let margins = parseInt(cs.marginLeft) + parseInt(cs.marginRight);
  return popup.parentNode.getBoundingClientRect().width + margins;
}

function closeAllChats() {
  let chatbar = window.SocialChatBar.chatbar;
  chatbar.removeAll();
}
