/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2001
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/////////////////////////////////////////////////////////////////////////////

#define NS_BROWSERPROFILEMIGRATOR_CONTRACTID_PREFIX "@mozilla.org/profile/migrator;1?app=browser&type="

#ifdef XP_WIN
#define NS_WINIEPROFILEMIGRATOR_CID \
{ 0xbc15c73d, 0xc05b, 0x497b, { 0xa3, 0x73, 0x4b, 0xae, 0x6c, 0x17, 0x86, 0x31 } }
#endif

#ifdef XP_MACOSX
#define NS_SAFARIPROFILEMIGRATOR_CID \
{ 0x29e3b139, 0xad19, 0x44f3, { 0xb2, 0xc2, 0xe9, 0xf1, 0x3b, 0xa2, 0xbb, 0xc6 } }

#define NS_MACIEPROFILEMIGRATOR_CID \
{ 0xf1a4e549, 0x5c4b, 0x41ff, { 0xb5, 0xe3, 0xeb, 0x87, 0xae, 0x31, 0x41, 0x9b } }

#define NS_OMNIWEBPROFILEMIGRATOR_CID \
{ 0xb80ae6d8, 0x766c, 0x43da, { 0x9c, 0x7a, 0xd, 0x82, 0x44, 0x52, 0x61, 0x6a } }

#define NS_CAMINOPROFILEMIGRATOR_CID \
{ 0x01d88ea9, 0x0feb, 0x495e, { 0x8c, 0x9b, 0x41, 0x65, 0x99, 0x55, 0x52, 0x65 } }

#define NS_ICABPROFILEMIGRATOR_CID \
{ 0xf394a036, 0xc5e1, 0x46d8, { 0x99, 0x39, 0x6b, 0x35, 0xe1, 0x13, 0x0a, 0x27 } }

#endif

#define NS_OPERAPROFILEMIGRATOR_CID \
{ 0xf34ff792, 0x722e, 0x4490, { 0xb1, 0x95, 0x47, 0xd2, 0x42, 0xed, 0xca, 0x1c } }

#define NS_DOGBERTPROFILEMIGRATOR_CID \
{ 0x24f92fae, 0xf793, 0x473b, { 0x80, 0x61, 0x71, 0x34, 0x8, 0xbd, 0x11, 0xd5 } }

#define NS_SEAMONKEYPROFILEMIGRATOR_CID \
{ 0x9a28ffa7, 0xe6ef, 0x4b52, { 0xa1, 0x27, 0x6a, 0xd9, 0x51, 0xde, 0x8e, 0x9b } }

#define NS_PHOENIXPROFILEMIGRATOR_CID \
{ 0x78481e4a, 0x50e4, 0x4489, { 0xb6, 0x8a, 0xef, 0x82, 0x67, 0xe, 0xd6, 0x3f } }

#define NS_SHELLSERVICE_CID \
{ 0x63c7b9f4, 0xcc8, 0x43f8, { 0xb6, 0x66, 0xa, 0x66, 0x16, 0x55, 0xcb, 0x73 } }

#define NS_SHELLSERVICE_CONTRACTID \
  "@mozilla.org/browser/shell-service;1"

#define NS_RDF_FORWARDPROXY_INFER_DATASOURCE_CID \
{ 0x7a024bcf, 0xedd5, 0x4d9a, { 0x86, 0x14, 0xd4, 0x4b, 0xe1, 0xda, 0xda, 0xd3 } }

#define NS_FEEDSNIFFER_CID \
{ 0x6893e69, 0x71d8, 0x4b23, { 0x81, 0xeb, 0x80, 0x31, 0x4d, 0xaf, 0x3e, 0x66 } }

#define NS_FEEDSNIFFER_CONTRACTID \
  "@mozilla.org/browser/feeds/sniffer;1"

#define NS_ABOUTFEEDS_CID \
{ 0x12ff56ec, 0x58be, 0x402c, { 0xb0, 0x57, 0x1, 0xf9, 0x61, 0xde, 0x96, 0x9b } }

// 136e2c4d-c5a4-477c-b131-d93d7d704f64
#define NS_PRIVATE_BROWSING_SERVICE_WRAPPER_CID \
{ 0x136e2c4d, 0xc5a4, 0x477c, { 0xb1, 0x31, 0xd9, 0x3d, 0x7d, 0x70, 0x4f, 0x64 } }

// 7e4bb6ad-2fc4-4dc6-89ef-23e8e5ccf980
#define NS_BROWSER_ABOUT_REDIRECTOR_CID \
{ 0x7e4bb6ad, 0x2fc4, 0x4dc6, { 0x89, 0xef, 0x23, 0xe8, 0xe5, 0xcc, 0xf9, 0x80 } }

// {6DEB193C-F87D-4078-BC78-5E64655B4D62}
#define NS_BROWSERDIRECTORYPROVIDER_CID \
{ 0x6deb193c, 0xf87d, 0x4078, { 0xbc, 0x78, 0x5e, 0x64, 0x65, 0x5b, 0x4d, 0x62 } }
