// This caused security exceptions in the past, make sure it doesn't!
var myConstructor = {}.constructor;

// Try to call a function defined in the imported script.
function importedScriptFunction() {
}
