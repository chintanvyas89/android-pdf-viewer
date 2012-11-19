#!/usr/bin/env pypy

import os, sys, logging, re
import argparse
import fnmatch



configurations = {'lite', 'pro'}


package_dirs = {
    'lite': ('src/cx/hell/android/pdfview',),
    'pro': ('src/cx/hell/android/pdfviewpro',)
}


file_replaces = {
    'lite': (
        'cx.hell.android.pdfview.',
        '"cx.hell.android.pdfview"',
        'package cx.hell.android.pdfview;',
        'android:icon="@drawable/pdfviewer"',
    ),
    'pro': (
        'cx.hell.android.pdfviewpro.',
        '"cx.hell.android.pdfviewpro"',
        'package cx.hell.android.pdfviewpro;',
        'android:icon="@drawable/apvpro_icon"',
    ),
}


def make_comment(file_type, line):
    """Add comment to line and return modified line, but try not to add comments to already commented out lines."""
    if file_type in ('java', 'c'):
        return '// ' + line if not line.startswith('//') else line
    elif file_type in ('html', 'xml'):
        return '<!-- ' + line.strip() + ' -->\n' if not line.strip().startswith('<!--') else line
    else:
        raise Exception("unknown file type: %s" % file_type)


def remove_comment(file_type, line):
    """Remove comment from line, but only if line is commented, otherwise return unchanged line."""
    if file_type in ('java', 'c'):
        if line.startswith('// '): return line[3:]
        else: return line
    elif file_type in ('html', 'xml'):
        if line.strip().startswith('<!-- ') and line.strip().endswith(' -->'): return line.strip()[5:-4] + '\n'
        else: return line
    else:
        raise Exception("unknown file type: %s" % file_type)


def handle_comments(conf, file_type, lines, filename):
    new_lines = []
    re_cmd_starts = re.compile(r'(?:(//|<!--))\s+#ifdef\s+(?P<def>[a-zA-Z]+)')
    re_cmd_ends = re.compile(r'(?:(//|<!--))\s+#endif')
    required_defs = []
    for i, line in enumerate(lines):
        m = re_cmd_starts.search(line)
        if m:
            required_def = m.group('def')
            logging.debug("line %s:%d %s matches as start of %s" % (filename, i+1, line.strip(), required_def))
            required_defs.append(required_def)
            new_lines.append(line)
            continue
        m = re_cmd_ends.search(line)
        if m:
            logging.debug("line %s:%d %s matches as endif" % (filename, i+1, line.strip()))
            required_defs.pop()
            new_lines.append(line)
            continue
        if len(required_defs) == 0:
            new_lines.append(line)
        elif len(required_defs) == 1 and required_defs[0] == conf:
            new_line = remove_comment(file_type, line)
            new_lines.append(new_line)
        else:
            new_line = make_comment(file_type, line)
            new_lines.append(new_line)
    assert len(new_lines) == len(lines)
    return new_lines


def find_files(dirname, name):
    matches = []
    for root, dirnames, filenames in os.walk(dirname):
        for filename in fnmatch.filter(filenames, name):
            matches.append(os.path.join(root, filename))
    return matches


def fix_package_dirs(conf):
    for i, dirname in enumerate(package_dirs[conf]):
        logging.debug("trying to restore %s" % dirname)
        if os.path.exists(dirname):
            if os.path.isdir(dirname):
                logging.debug(" already exists")
                continue
            else:
                logging.error(" %s already exists, but is not dir" % dirname)
                continue
        # find other name
        found_dirname = None
        for other_conf, other_dirnames in package_dirs.items():
            other_dirname = other_dirnames[i]
            if other_conf == conf: continue # skip this conf when looking for other conf
            if os.path.isdir(other_dirname):
                if found_dirname is None:
                    found_dirname = other_dirname
                else:
                    # source dir already found :/
                    raise Exception("too many possible dirs for this package: %s, %s" % (found_dirname, other_dirname))
        if found_dirname is None:
            raise Exception("didn't find %s" % dirname)
        # now rename found_dirname to dirname
        os.rename(found_dirname, dirname)
        logging.debug("renamed %s to %s" % (found_dirname, dirname))


def handle_comments_in_files(conf, file_type, filenames):
    for filename in filenames:
        lines = open(filename).readlines()
        new_lines = handle_comments(conf, file_type, lines, filename)
        if lines != new_lines:
            logging.debug("file %s comments changed" % filename)
            f = open(filename, 'w')
            f.write(''.join(new_lines))
            f.close()
            del f


def replace_in_files(conf, filenames):
    #logging.debug("about replace to %s in %s" % (conf, ', '.join(filenames)))
    other_confs = [other_conf for other_conf in file_replaces.keys() if other_conf != conf]
    #logging.debug("there are %d other confs to replace from: %s" % (len(other_confs), ', '.join(other_confs)))
    for filename in filenames:
        new_lines = []
        lines = open(filename).readlines()
        for line in lines:
            new_line = line
            for i, target_string in enumerate(file_replaces[conf]):
                for other_conf in other_confs:
                    source_string = file_replaces[other_conf][i]
                    new_line = new_line.replace(source_string, target_string)
            new_lines.append(new_line)
        if new_lines != lines:
            logging.debug("file %s changed, writing..." % filename)
            f = open(filename, 'w')
            f.write(''.join(new_lines))
            f.close()
            del f
        else:
            logging.debug("file %s didn't change, no need to rewrite" % filename)


def fix_java_files(conf):
    filenames = find_files('src', name='*.java')
    replace_in_files(conf, filenames)
    handle_comments_in_files(conf, 'java', filenames)

def fix_xml_files(conf):
    filenames = find_files('.', name='*.xml')
    replace_in_files(conf, filenames)
    handle_comments_in_files(conf, 'xml', filenames)


def fix_html_files(conf):
    filenames = find_files('res', name='*.html')
    replace_in_files(conf, filenames)
    handle_comments_in_files(conf, 'html', filenames)


def fix_c_files(conf):
    filenames = find_files('jni/pdfview2', name='*.c')
    replace_in_files(conf, filenames)
    handle_comments_in_files(conf, 'c', filenames)

    filenames = find_files('jni/pdfview2', name='*.h')
    replace_in_files(conf, filenames)
    handle_comments_in_files(conf, 'c', filenames)


def fix_resources(conf):
    pass


def main():
    logging.basicConfig(level=logging.DEBUG, format='%(asctime)s %(levelname)s %(message)s')
    parser = argparse.ArgumentParser(description='Switch project configurations')
    parser.add_argument('--configuration', dest='configuration', default='lite')
    args = parser.parse_args()

    if not os.path.exists('AndroidManifest.xml'):
        raise Exception('android manifest not found, please run this script from main project directory')

    conf = args.configuration
    if conf not in configurations:
        raise Exception("invalid configuration: %s" % conf)

    fix_package_dirs(conf)
    fix_java_files(conf)
    fix_xml_files(conf)
    fix_html_files(conf)
    fix_c_files(conf)
    fix_resources(conf)
    



if __name__ == '__main__':
    main()
