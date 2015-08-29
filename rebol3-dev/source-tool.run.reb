REBOL []

do %source-tool.reb


output-folder: %source-tool.draft/
core-output: output-folder/%core/

make-dir output-folder
make-dir core-output

write output-folder/%README.md {THIS FOLDER AND CONTENTS ARE AUTOMATICALLY GENERATED^/}

source-tool/logfile: output-folder/%source-tool.log.txt
source-tool/core.output.folder: core-output

source-tool/update/all