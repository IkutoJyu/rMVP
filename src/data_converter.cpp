// Data pre-processing module
// 
// Copyright (C) 2016-2018 by Xiaolei Lab
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
// http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include <omp.h>
#include <Rcpp.h>
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
#include <progress.hpp>
#include <fstream>

using namespace std;
using namespace Rcpp;

// [[Rcpp::plugins(cpp11)]]
// [[Rcpp::depends(BH, bigmemory)]]
// [[Rcpp::plugins(openmp)]]
// [[Rcpp::depends(RcppProgress)]]

#include <bigmemory/isna.hpp>
#include <bigmemory/MatrixAccessor.hpp>


// *** Utility ***
int omp_setup(int threads=0, bool verbose=true) {
    int t = 1;
#ifdef _OPENMP
    if (threads == 0) {
        t = omp_get_num_procs() - 1;
        t = t > 0 ? t : 1;
    } else if (threads > 0) {
        t = threads;
    }
    omp_set_num_threads(t);
    
    if (verbose) {
        Rcerr << "Number of threads: " << omp_get_max_threads() << endl;
    }
#else
    if (verbose) {
        Rcerr << "Number of threads: 1 (No OpneMP detected)" << endl;
    }
#endif
    return t;
}


// ***** VCF *****

// [[Rcpp::export]]
List vcf_parser_map(std::string vcf_file, std::string out) {
    // Define
    const int MAP_INFO_N = 50;      // max length of "SNP, POS and CHROM"
    ifstream file(vcf_file);
    ofstream map(out + ".map");
    ofstream indfile(out + ".geno.ind");
    
    string line;
    vector<string> l;
    vector<string> ind;
    size_t n, m;
    
    // Skip Header
    string prefix("#CHROM");
    bool have_header = false;
    while (file) {
        getline(file, line);
        if (!line.compare(0, prefix.size(), prefix)) {
            have_header = true;
            break; 
        }
    }
    if (!have_header) {
        Rcpp::stop("ERROR: Wrong VCF file, no line begin with \"#CHROM\".");
    }
    
    // Write inds to file.
    boost::split(ind, line, boost::is_any_of("\t"));
    vector<string>(ind.begin() + 9, ind.end()).swap(ind);   // delete first 9 columns
    for (int i = 0; i < ind.size(); i++) {
        indfile << ind[i] << endl;
    }
    n = ind.size();
    indfile.close();

    // unsigned m = count(                 // 3-4s per 100Mb
    //     istream_iterator<char>(file),
    //     istream_iterator<char>(),
    //     '\n');
    // file.seekg(pos);
    
    // Write inds to file.
    map << "SNP\tCHROM\tPOS"<< endl;
    m = 0;
    while (getline(file, line)) {
        string tmp = line.substr(0, MAP_INFO_N);
        boost::split(l, tmp, boost::is_any_of("\t"));

        if (l[2] == ".") {      // snp name missing
            l[2] = l[0] + '-' + l[1];
        }
        map << l[2] << '\t' << l[0] << '\t' << l[1] << endl;
        m++;
    }
    map.close();
    file.close();
    
    return List::create(_["n"] = n,
                        _["m"] = m);
}

template <typename T>
T vcf_marker_parser(string m, double NA_C) {
    if (('0' == m[0] || '1' == m[0]) && ('0' == m[2] || '1' == m[2])) {
        return static_cast<T>(m[0] - '0' + m[2] - '0');
    } else {
        return static_cast<T>(NA_C);
    }
}

template <typename T>
void vcf_parser_genotype(std::string vcf_file, XPtr<BigMatrix> pMat, long maxLine, double NA_C, int threads=0, bool verbose=true) {
    // define
    ifstream file(vcf_file);
    
    omp_setup(threads, verbose);
    string line;
    vector<string> l;
    vector<char> markers;
    size_t m;
    MatrixAccessor<T> mat = MatrixAccessor<T>(*pMat);
    
    // progress bar
    Progress progress(pMat->nrow(), verbose);
    
    // Skip Header
    string prefix("#CHROM");
    bool have_header = false;
    while (file) {
        getline(file, line);
        if (!line.compare(0, prefix.size(), prefix)) {
            have_header = true;
            break;
        }
    }
    if (!have_header) {
        Rcpp::stop("ERROR: Wrong VCF file, no line begin with \"#CHROM\".");
    }
    
    // parser genotype
    m = 0;
    vector<string> buffer;
    while (file) {
        buffer.clear();
        for (int i = 0; file && (maxLine <= 0 || i < maxLine); i++) {
            getline(file, line);
            if (line.length() > 1) {    // Handling the blank line at the end of the file.
                buffer.push_back(line);
            }
        }
        #pragma omp parallel for private(l, markers)
        for (int i = 0; i < buffer.size(); i++) {
            boost::split(l, buffer[i], boost::is_any_of("\t"));
            markers.clear();
            // There is only one char in REF and ALT
            // if (l[3].length() == 1 && l[4].length() == 1) {  
                vector<string>(l.begin() + 9, l.end()).swap(l);
                transform(
                    l.begin(), l.end(), 
                    back_inserter(markers),
                    boost::bind<T>(&vcf_marker_parser<T>, _1, NA_C)
                );
            // } else {
            //     // Delete multiple variant sites
            //     fill_n(markers.begin(), l.size() - 9, NA_C);
            // }
            
            for (int j = 0; j < markers.size(); j++) {
                mat[j][m + i] = markers[j];
            }
            progress.increment();
        }
        m += buffer.size();
    }
}

// [[Rcpp::export]]
void vcf_parser_genotype(std::string vcf_file, SEXP pBigMat, long maxLine, int threads=0, bool verbose=true) {
    XPtr<BigMatrix> xpMat(pBigMat);
    
    switch(xpMat->matrix_type()) {
    case 1:
        return vcf_parser_genotype<char>(vcf_file, xpMat, maxLine, NA_CHAR, threads, verbose);
    case 2:
        return vcf_parser_genotype<short>(vcf_file, xpMat, maxLine, NA_SHORT, threads, verbose);
    case 4:
        return vcf_parser_genotype<int>(vcf_file, xpMat, maxLine, NA_INTEGER, threads, verbose);
    case 8:
        return vcf_parser_genotype<double>(vcf_file, xpMat, maxLine, NA_REAL, threads, verbose);
    default:
        throw Rcpp::exception("unknown type detected for big.matrix object!");
    }
}


// ***** HAPMAP *****

// [[Rcpp::export]]
List hapmap_parser_map(Rcpp::StringVector hmp_file, std::string out) {
    // Define
    const int MAP_INFO_N = 50;      // max length of "SNP, POS and CHROM"
    ofstream map(out + ".map");
    ofstream indfile(out + ".geno.ind");
    
    string line;
    vector<string> l;
    vector<string> ind;
    size_t n;
    size_t m;
    
    for (int i = 0; i < hmp_file.size(); i++) { // TODO(haohao): Support mulit hmp file.
        ifstream file(hmp_file(i));
        
        // Skip Header
        string prefix("rs#");
        bool have_header = false;
        while (file) {
            getline(file, line);
            if (!line.compare(0, prefix.size(), prefix)) {
                have_header = true;
                break; 
            }
        }
        if (!have_header) {
            Rcpp::stop("ERROR: Wrong HAPMAP file, no line begin with \"rs#\".");
        }
        
        // Write inds to file.
        boost::split(ind, line, boost::is_any_of("\t"));
        vector<string>(ind.begin() + 11, ind.end()).swap(ind);   // delete first 11 columns
        for (int i = 0; i < ind.size(); i++) {
            indfile << ind[i] << endl;
        }
        n = ind.size();
        indfile.close();
        
        // Write SNPs to map file.
        map << "SNP\tCHROM\tPOS"<< endl;
        m = 0;
        while (getline(file, line)) {
            string tmp = line.substr(0, MAP_INFO_N);
            boost::split(l, tmp, boost::is_any_of("\t"));
            
            if (l[0] == ".") {      // snp name missing
                l[0] = l[2] + '-' + l[3];
            }
            map << l[0] << '\t' << l[2] << '\t' << l[3] << endl;
            m++;
        }
        map.close();
        file.close();
        
    }
    
    
    return List::create(_["n"] = n,
                        _["m"] = m);
}

template <typename T>
T hapmap_marker_parser(string m, char major, double NA_C) {
    if (m.length() == 1) {  // Hapmap
        if (m[0] == '+' || m[0] == '0' || m[0] == '-' || m[0] == 'N') {
            return static_cast<T>(NA_C);
        } else if (m[0] == major) {
            return 0;
        } else if (m[0] == 'R' || m[0] == 'Y' || m[0] == 'S' || m[0] == 'W' || m[0] == 'K' || m[0] == 'M') {
            return 1;
        } else if (m[0] == 'A' || m[0] == 'T' || m[0] == 'G' || m[0] == 'C') {
            return 2;
        }
    } else if (m.length() == 2) {   // Hapmap Diploid
        if ((m[0] != 'A' && m[0] != 'T' && m[0] != 'G' && m[0] != 'C') ||
            (m[1] != 'A' && m[1] != 'T' && m[1] != 'G' && m[1] != 'C')) {
            return static_cast<T>(NA_C);
        } else {
            return static_cast<T>(
                ((m[0] == major)?0:1) + ((m[1] == major)?0:1)
            );
        }
    }
    return static_cast<T>(NA_C);
}

template <typename T>
void hapmap_parser_genotype(std::string hmp_file, XPtr<BigMatrix> pMat, double NA_C, bool verbose=true) {
    // define
    ifstream file(hmp_file);
    
    string line;
    char major;
    vector<string> l;
    vector<char> markers;
    size_t m;
    MatrixAccessor<T> mat = MatrixAccessor<T>(*pMat);
    
    // progress bar
    Progress progress(pMat->nrow(), verbose);
    
    // Skip Header
    string prefix("rs#");
    bool have_header = false;
    while (file) {
        getline(file, line);
        if (!line.compare(0, prefix.size(), prefix)) {
            have_header = true;
            break; 
        }
    }

    if (!have_header) {
        Rcpp::stop("ERROR: Wrong HAPMAP file, no line begin with \"rs#\".");
    }
    
    // parser genotype
    m = 0;
    while (getline(file, line)) {
        boost::split(l, line, boost::is_any_of("\t"));
        major = l[1][0];
        if (l[1].length() > 3) {
            major = 'N';
        }
        vector<string>(l.begin() + 11, l.end()).swap(l);
        markers.clear();
        transform(
            l.begin(),
            l.end(),
            back_inserter(markers),
            boost::bind<T>(&hapmap_marker_parser<T>, _1, major, NA_C)
        );
        for (size_t i = 0; i < markers.size(); i++) {
            mat[i][m] = markers[i];
        }
        m++;
        progress.increment();
    }
}

// [[Rcpp::export]]
void hapmap_parser_genotype(std::string hmp_file, SEXP pBigMat, bool verbose=true) {
    XPtr<BigMatrix> xpMat(pBigMat);
    
    switch(xpMat->matrix_type()) {
    case 1:
        return hapmap_parser_genotype<char>(hmp_file, xpMat, NA_CHAR, verbose);
    case 2:
        return hapmap_parser_genotype<short>(hmp_file, xpMat, NA_SHORT, verbose);
    case 4:
        return hapmap_parser_genotype<int>(hmp_file, xpMat, NA_INTEGER, verbose);
    case 8:
        return hapmap_parser_genotype<double>(hmp_file, xpMat, NA_REAL, verbose);
    default:
        throw Rcpp::exception("unknown type detected for big.matrix object!");
    }
}


// ***** NUMERIC *****

// [[Rcpp::export]]
List numeric_scan(std::string num_file) {
    // Define
    string line;
    vector<string> l;
    vector<string> ind;
    size_t n, m;
    
    ifstream file(num_file);
    
    getline(file, line);
    boost::split(l, line, boost::is_any_of("\t ,"));

    n = l.size();
    m = 1;
    while (getline(file, line))
        m++;
    
    return List::create(_["n"] = n,
                        _["m"] = m);
}


// ***** BFILE *****

template <typename T>
void write_bfile(XPtr<BigMatrix> pMat, std::string bed_file, double NA_C, int threads=0, bool verbose=true) {
    // check input
    string ending = ".bed";
    if (bed_file.length() <= ending.length() ||
        0 != bed_file.compare(bed_file.length() - ending.length(), ending.length(), ending)) {
        bed_file += ending;
    }
    
    // define
    T c;
    omp_setup(threads, verbose);
    long m = pMat->nrow();
    long n = pMat->ncol() / 4;  // 4 individual = 1 bit
    if (pMat->ncol() % 4 != 0) 
        n++;
    
    vector<uint8_t> geno(n);
    MatrixAccessor<T> mat = MatrixAccessor<T>(*pMat);
    FILE *fout;
    fout = fopen(bed_file.c_str(), "wb");
    
    // progress bar
    Progress progress(n, verbose);
    
    // magic number of bfile
    const unsigned char magic_bytes[] = { 0x6c, 0x1b, 0x01 };
    fwrite((char*)magic_bytes, 1, 3, fout);
    
    // map
    std::map<T, int> code;
    code[0] = 3;
    code[1] = 2;
    code[2] = 0;
    code[static_cast<T>(NA_C)] = 1;
    
    // write bfile
    for (size_t i = 0; i < m; i++) {
        #pragma omp parallel for
        for (size_t j = 0; j < n; j++) {
            uint8_t p = 0;
            for (size_t x = 0; x < 4 && (4 * j + x) < pMat->ncol(); x++) {
                c = mat[4 * j + x][i];
                p |= code[c] << (x*2);
            }
            geno[j] = p;
        }
        fwrite((char*)geno.data(), 1, geno.size(), fout);
        progress.increment();
    }
    fclose(fout);
    return;
}

// [[Rcpp::export]]
void write_bfile(SEXP pBigMat, std::string bed_file, int threads=0, bool verbose=true) {
    XPtr<BigMatrix> xpMat(pBigMat);
    
    switch(xpMat->matrix_type()) {
    case 1:
        return write_bfile<char>(xpMat, bed_file, NA_CHAR, threads, verbose);
    case 2:
        return write_bfile<short>(xpMat, bed_file, NA_SHORT, threads, verbose);
    case 4:
        return write_bfile<int>(xpMat, bed_file, NA_INTEGER, threads, verbose);
    case 8:
        return write_bfile<double>(xpMat, bed_file, NA_REAL, threads, verbose);
    default:
        throw Rcpp::exception("unknown type detected for big.matrix object!");
    }
}

template <typename T>
void read_bfile(std::string bed_file, XPtr<BigMatrix> pMat, long maxLine, double NA_C, int threads=0, bool verbose=true) {
    // check input
    string ending = ".bed";
    if (bed_file.length() <= ending.length() ||
        0 != bed_file.compare(bed_file.length() - ending.length(), ending.length(), ending))
        bed_file += ending;
    
    // define
    omp_setup(threads, verbose);
    long n = pMat->ncol() / 4;  // 4 individual = 1 bit
    if (pMat->ncol() % 4 != 0) 
        n++; 
    char *buffer;
    long buffer_size;
    MatrixAccessor<T> mat = MatrixAccessor<T>(*pMat);
    
    // map
    std::map<int, T> code;
    code[3] = 0;
    code[2] = 1;
    code[1] = static_cast<T>(NA_C);
    code[0] = 2;
    
    // open file
    FILE *fin;
    fin = fopen(bed_file.c_str(), "rb");
    fseek(fin, 0, SEEK_END);
    long length = ftell(fin);
    rewind(fin);
    
    // get buffer_size
    buffer_size = maxLine > 0 ? (maxLine * n) : (length - 3);
    
    // progress bar
    int n_block = (length - 3) / buffer_size;
    if ((length - 3) % buffer_size != 0) { n_block++; }
    Progress progress(n_block, verbose);
    
    // magic number of bfile
    buffer = new char [3];
    fread(buffer, 1, 3, fin);
    
    // loop file
    #pragma omp parallel for
    for (int i = 0; i < n_block; i++) {
        buffer = new char [buffer_size];
        fread(buffer, 1, buffer_size, fin);
        
        size_t r, c;
        // i: current block start, j: current bit.
        for (size_t j = 0; j < buffer_size && i * buffer_size + j < length - 3; j++) {
            // bit -> item in matrix
            r = (i * buffer_size + j) / n;
            c = (i * buffer_size + j) % n * 4;
            uint8_t p = buffer[j];
            
            for (size_t x = 0; x < 4 && (c + x) < pMat->ncol(); x++) {
                mat[c + x][r] = code[(p >> (2*x)) & 0x03];
            }
        }
        progress.increment();
    }
    fclose(fin);
    return;
}

// [[Rcpp::export]]
void read_bfile(std::string bed_file, SEXP pBigMat, long maxLine, int threads=0, bool verbose=true) {
    XPtr<BigMatrix> xpMat(pBigMat);
    
    switch(xpMat->matrix_type()) {
    case 1:
        return read_bfile<char>(bed_file, xpMat, maxLine, NA_CHAR, threads, verbose);
    case 2:
        return read_bfile<short>(bed_file, xpMat, maxLine, NA_SHORT, threads, verbose);
    case 4:
        return read_bfile<int>(bed_file, xpMat, maxLine, NA_INTEGER, threads, verbose);
    case 8:
        return read_bfile<double>(bed_file, xpMat, maxLine, NA_REAL, threads, verbose);
    default:
        throw Rcpp::exception("unknown type detected for big.matrix object!");
    }
}

template <typename T>
Rcpp::NumericVector count_allele(XPtr<BigMatrix> pMat, int i) {
    MatrixAccessor<T> mat = MatrixAccessor<T>(*pMat);
    
    NumericVector counts = NumericVector::create(
        _["0"] = 0,
        _["1"] = 0,
        _["2"] = 0
    );
    
    auto n = pMat->ncol();
    for (size_t j = 0; j < n; j++) {
        switch(int(mat[j][i - 1])) {
            case 0: counts[0]++; break;
            case 1: counts[1]++; break;
            case 2: counts[2]++; break;
            default: break;
        }
    }
    return counts;
}

// [[Rcpp::export]]
Rcpp::NumericVector count_allele(SEXP pBigMat, int i) {
    XPtr<BigMatrix> xpMat(pBigMat);
    
    switch(xpMat->matrix_type()) {
    case 1:
        return count_allele<char>(xpMat, i);
    case 2:
        return count_allele<short>(xpMat, i);
    case 4:
        return count_allele<int>(xpMat, i);
    case 8:
        return count_allele<double>(xpMat, i);
    default:
        throw Rcpp::exception("unknown type detected for big.matrix object!");
    }
}

template <typename T>
bool hasNA(XPtr<BigMatrix> pMat, double NA_C) {
    MatrixAccessor<T> mat = MatrixAccessor<T>(*pMat);
    for (size_t j = 0; j < pMat->ncol(); j++) {
        for (size_t i = 0; i < pMat->nrow(); i++) {
            if (mat[j][i] == NA_C) {
                return true;
            }
        }
    }
    return false;
}

// [[Rcpp::export]]
bool hasNA(SEXP pBigMat) {
    XPtr<BigMatrix> xpMat(pBigMat);
    
    switch(xpMat->matrix_type()) {
    case 1:
        return hasNA<char>(xpMat, NA_CHAR);
    case 2:
        return hasNA<short>(xpMat, NA_SHORT);
    case 4:
        return hasNA<int>(xpMat, NA_INTEGER);
    case 8:
        return hasNA<double>(xpMat, NA_REAL);
    default:
        throw Rcpp::exception("unknown type detected for big.matrix object!");
    }
}
/*** R
# setwd("~/code/MVP/src")
library(bigmemory)

# # Test case
# # MVP.Data.vcf_parser('/Volumes/Hyacz-WD/Biodata/demo/vcf/maize.vcf', 'maize1')
# MVP.Data.vcf_parser('/Users/hyacz/code/MVP/demo.data/VCF/myVCF.vcf', 'mvp1')
# system.time({MVP.Data.impute('mvp1.geno.desc')})
# x <- attach.big.matrix('mvp1.geno.desc')
# x[1:5,1:5]

c <- big.matrix(3093, 279)
read_bfile('demo.data/bed/plink.bed', c@address, -1)
library(plink2R)
a <- read_plink('demo.data/bed/plink')
b <- unname(t(as.matrix(a$bed)))
b[1, 1:20]
c[1, 1:20]
write_bfile(c@address, 'test.bed')
# numeric_scan('/Users/hyacz/code/MVP/demo.data/numeric/mvp.hmp.Numeric.txt')
*/
