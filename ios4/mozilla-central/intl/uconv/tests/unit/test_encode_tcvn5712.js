// Tests conversion from Unicode to x-viet-tcvn5712

load('CharsetConversionTests.js');
	
const inString = "\u00da\u1ee4\u1eea\u1eec\u1eee\u1ee8\u1ef0\u1ef2\u1ef6\u1ef8\u00dd\u1ef4 !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~\u00c0\u1ea2\u00c3\u00c1\u1ea0\u1eb6\u1eac\u00c8\u1eba\u1ebc\u00c9\u1eb8\u1ec6\u00cc\u1ec8\u0128\u00cd\u1eca\u00d2\u1ece\u00d5\u00d3\u1ecc\u1ed8\u1edc\u1ede\u1ee0\u1eda\u1ee2\u00d9\u1ee6\u0168\u00a0\u0102\u00c2\u00ca\u00d4\u01a0\u01af\u0110\u0103\u00e2\u00ea\u00f4\u01a1\u01b0\u0111\u1eb0\u0300\u0309\u0303\u0301\u0323\u00e0\u1ea3\u00e3\u00e1\u1ea1\u1eb2\u1eb1\u1eb3\u1eb5\u1eaf\u1eb4\u1eae\u1ea6\u1ea8\u1eaa\u1ea4\u1ec0\u1eb7\u1ea7\u1ea9\u1eab\u1ea5\u1ead\u00e8\u1ec2\u1ebb\u1ebd\u00e9\u1eb9\u1ec1\u1ec3\u1ec5\u1ebf\u1ec7\u00ec\u1ec9\u1ec4\u1ebe\u1ed2\u0129\u00ed\u1ecb\u00f2\u1ed4\u1ecf\u00f5\u00f3\u1ecd\u1ed3\u1ed5\u1ed7\u1ed1\u1ed9\u1edd\u1edf\u1ee1\u1edb\u1ee3\u00f9\u1ed6\u1ee7\u0169\u00fa\u1ee5\u1eeb\u1eed\u1eef\u1ee9\u1ef1\u1ef3\u1ef7\u1ef9\u00fd\u1ef5\u1ed0";
    
const expectedString = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~��������������������������������������������������������������������������������������������������������������������������������";

const aliases = [ "x-viet-tcvn5712" ];

function run_test() {
  testEncodeAliases();
}
