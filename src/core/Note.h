#pragma once

#include <QString>

// A single note in the vault. Notes are plain .md files on disk;
// the "title" is the file name without its .md extension.
struct Note {
    QString path;   // absolute path to the .md file
    QString title;  // file stem, used as the link target name
};
