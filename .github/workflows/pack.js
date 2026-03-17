// pack.js — run after windeployqt to produce a single exe via Enigma Virtual Box
// Usage: node pack.js <exeDir> <outputExe>
//   exeDir    = folder containing OpenMTR2.exe + all DLLs (e.g. bin\x64\Release)
//   outputExe = where to write the packed single exe (e.g. OpenMTR2.exe)
const path = require('path');
const generateEvb = require('generate-evb');
const evb = require('enigmavirtualbox');

const exeDir    = path.resolve(process.argv[2]);
const outputExe = path.resolve(process.argv[3]);
const inputExe  = path.join(exeDir, 'OpenMTR2.exe');
const evbFile   = path.join(path.dirname(outputExe), 'OpenMTR2.evb');

console.log('Input exe :', inputExe);
console.log('Output exe:', outputExe);
console.log('Pack dir  :', exeDir);
console.log('EVB file  :', evbFile);

generateEvb(evbFile, inputExe, outputExe, exeDir, {
    compressFiles: true,
    deleteExtractedOnExit: true
});
console.log('EVB project generated');

evb.cli(evbFile).then(r => {
    console.log(r.stdout);
    if (r.stderr) console.error(r.stderr);
    if (r.code && r.code !== 0) process.exit(1);
}).catch(e => {
    console.error(e);
    process.exit(1);
});
