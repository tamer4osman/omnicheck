const { helper } = require("./util");
const extra = require("./nested/extra.js");
const path = require("path");

function runImports(n) {
  return helper(n) + extra(n) + (path ? 0 : 1);
}

module.exports = { runImports };
