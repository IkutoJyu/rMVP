% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/MVP.Data.r
\name{MVP.Data.Kin}
\alias{MVP.Data.Kin}
\title{Kinship}
\usage{
MVP.Data.Kin(fileKin = TRUE, mvp_prefix = "mvp", out = NULL,
  maxLine = 10000, priority = "speed", sep = "\\t")
}
\arguments{
\item{fileKin}{Kinship that represents relationship among individuals, n * n matrix, n is sample size}

\item{mvp_prefix}{Prefix for mvp format files}

\item{out}{prefix of output file name}

\item{maxLine}{number of SNPs, only used for saving memory when calculate kinship matrix}

\item{priority}{"speed" or "memory"}

\item{sep}{seperator for Kinship file.}
}
\value{
Output file:
<out>.kin.bin
<out>.kin.desc
}
\description{
Kinship
}
\examples{
geno <- file.path(system.file("extdata", "06_mvp-impute", package = "rMVP"), "mvp.imp")
MVP.Data.Kin(TRUE, mvp_prefix=geno, out="rMVP.test.kin")
}
