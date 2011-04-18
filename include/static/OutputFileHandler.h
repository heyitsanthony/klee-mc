#ifndef SLICER_OUTPUT_FILE_HANDLER
#define SLICER_OUTPUT_FILE_HANDLER

#include "llvm/System/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <vector>
#include <string>
#include <map>
#include <fstream>
#include <iostream>
#include <set>
#include <iostream>
#include <fstream>
#include <cerrno>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/times.h>
#include <unistd.h>
#include <signal.h>

#include <iostream>
#include <iterator>
#include <fstream>
#include <sstream>

using namespace llvm;

class OutputFileHandler {
public:
    char m_outputDirectory[1024];

    OutputFileHandler(std::string OutputDir, std::string InputFile) {
        std::string theDir;

        if (OutputDir == "") {
            llvm::sys::Path directory(InputFile);            
            directory.eraseComponent();

            if (directory.isEmpty())
                directory.set(".");

            for (int i = 0;; i++) {
                char buf[256], tmp[64];
                sprintf(tmp, "slicer-out-%d", i);
                sprintf(buf, "%s/%s", directory.c_str(), tmp);
                theDir = buf;

                if (DIR * dir = opendir(theDir.c_str())) {
                    closedir(dir);
                } else {
                    break;
                }
            }
        } else {
            theDir = OutputDir;
        }

        /*
        llvm::sys::Path slicer_last(theDir);
        slicer_last.eraseComponent();
        slicer_last.appendComponent("slicer-last");

        if ((unlink(slicer_last.c_str()) < 0) && (errno != ENOENT)) {
            cout << "SLICER: ERROR: Cannot unlink slicer-last";
            assert(0 && "exiting.");
        }

        if (symlink(theDir.c_str(), slicer_last.c_str()) < 0) {
            cout << "SLICER: ERROR: Cannot make symlink: " << theDir << "\n" << slicer_last << std::endl;
            assert(0 && "exiting.");
        }
*/
        
        sys::Path p(theDir);
        if (!p.isAbsolute()) {
            sys::Path cwd = sys::Path::GetCurrentDirectory();
            cwd.appendComponent(theDir);
            p = cwd;
        }
        strcpy(m_outputDirectory, p.c_str());
        mkdir(m_outputDirectory, 0775);
    }

    std::ostream *openOutputFile(const std::string &filename) {                
        std::string path = getOutputFilename(filename);
        std::ostream *f = new std::ofstream(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
        if (!f) {
            std::cout << "SLICER: WARNING: out of memory: " << std::endl;
        } else if (!f->good()) {
            std::cout << "SLICER: WARNING: error opening: " << filename.c_str() << std::endl;
            delete f;
            f = NULL;
        }

        return f;
    }

    raw_fd_ostream *openRawOutputFile(const std::string &filename, std::string& errinfo) {
        std::string path = getOutputFilename(filename);
        raw_fd_ostream *f = new raw_fd_ostream(path.c_str(), false, false, errinfo);
        return f;
    }

    std::string getOutputFilename(const std::string &filename) {
        char outfile[1024];
        sprintf(outfile, "%s/%s", m_outputDirectory, filename.c_str());        
        return outfile;

    }
};


#endif
