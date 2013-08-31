/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://www.w3.org/TR/2012/WD-dom-20120105/
 *
 * Copyright © 2012 W3C® (MIT, ERCIM, Keio), All Rights Reserved. W3C
 * liability, trademark and document use rules apply.
 */

interface NamedNodeMap;
interface Principal;
interface URI;
interface UserDataHandler;

interface Node : EventTarget {
  const unsigned short ELEMENT_NODE = 1;
  const unsigned short ATTRIBUTE_NODE = 2; // historical
  const unsigned short TEXT_NODE = 3;
  const unsigned short CDATA_SECTION_NODE = 4; // historical
  const unsigned short ENTITY_REFERENCE_NODE = 5; // historical
  const unsigned short ENTITY_NODE = 6; // historical
  const unsigned short PROCESSING_INSTRUCTION_NODE = 7;
  const unsigned short COMMENT_NODE = 8;
  const unsigned short DOCUMENT_NODE = 9;
  const unsigned short DOCUMENT_TYPE_NODE = 10;
  const unsigned short DOCUMENT_FRAGMENT_NODE = 11;
  const unsigned short NOTATION_NODE = 12; // historical
  readonly attribute unsigned short nodeType;
  readonly attribute DOMString nodeName;

  readonly attribute DOMString? baseURI;

  readonly attribute Document? ownerDocument;
  readonly attribute Node? parentNode;
  readonly attribute Element? parentElement;
  boolean hasChildNodes();
  readonly attribute NodeList childNodes;
  readonly attribute Node? firstChild;
  readonly attribute Node? lastChild;
  readonly attribute Node? previousSibling;
  readonly attribute Node? nextSibling;

  [SetterThrows]
           attribute DOMString? nodeValue;
  [SetterThrows]
           attribute DOMString? textContent;
  [Throws]
  Node insertBefore(Node node, Node? child);
  [Throws]
  Node appendChild(Node node);
  [Throws]
  Node replaceChild(Node node, Node child);
  [Throws]
  Node removeChild(Node child);
  void normalize();

  [Throws]
  Node cloneNode(optional boolean deep = true);
  boolean isEqualNode(Node? node);

  const unsigned short DOCUMENT_POSITION_DISCONNECTED = 0x01;
  const unsigned short DOCUMENT_POSITION_PRECEDING = 0x02;
  const unsigned short DOCUMENT_POSITION_FOLLOWING = 0x04;
  const unsigned short DOCUMENT_POSITION_CONTAINS = 0x08;
  const unsigned short DOCUMENT_POSITION_CONTAINED_BY = 0x10;
  const unsigned short DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC = 0x20; // historical
  unsigned short compareDocumentPosition(Node other);
  boolean contains(Node? other);

  DOMString? lookupPrefix(DOMString? namespace);
  DOMString? lookupNamespaceURI(DOMString? prefix);
  boolean isDefaultNamespace(DOMString? namespace);

  // Mozilla-specific stuff
  // These have been moved to Element in the spec.
  readonly attribute NamedNodeMap? attributes;
  // If we move namespaceURI, prefix and localName to Element they should return
  // a non-nullable type.
  [Throws]
  readonly attribute DOMString? namespaceURI;
  readonly attribute DOMString? prefix;
  readonly attribute DOMString? localName;

  // This has been removed from the spec.
  boolean isSupported(DOMString feature, DOMString version);

  boolean hasAttributes();
  [Throws]
  any setUserData(DOMString key, any data, UserDataHandler? handler);
  [Throws]
  any getUserData(DOMString key);
  [ChromeOnly]
  readonly attribute Principal nodePrincipal;
  [ChromeOnly]
  readonly attribute URI? baseURIObject;
};
