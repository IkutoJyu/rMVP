% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/MVP.Report.R
\name{MVP.PCAplot}
\alias{MVP.PCAplot}
\title{PCA Plot}
\usage{
MVP.PCAplot(PCA, col = NULL, pch = NULL, class = NULL,
  legend.pos = "topright", Ncluster = 3, plot3D = TRUE,
  file = "pdf", dpi = 300, box = FALSE)
}
\arguments{
\item{PCA}{Principal component analysis result, 2-column matrix}

\item{col}{colors for each cluster}

\item{pch}{Either an integer specifying a symbol or a single character to be 
used as the default in plotting points. See \code{\link[graphics]{points}} 
for possible values and their interpretation. Note that only integers and
single-character strings can be set as a graphics parameter (and not NA 
nor NULL).}

\item{class}{the class of all individuals, for example: "breed", "location"...}

\item{legend.pos}{position of legend. default is "topright"}

\item{Ncluster}{cluster number}

\item{plot3D}{(DEPRECATED)if TRUE, plot PC figure in 3D format, it can be only used in windows and mac operation system, "rgl" package should be installed beforehead}

\item{file}{Character. Options are jpg, pdf, and tiff}

\item{dpi}{Number. Dots per inch for .jpg and .tiff files}

\item{box}{Logical value. If TRUE, the border line of Manhattan plot will be added}
}
\value{
Output file:
MVP.PCA_2D.<type>
}
\description{
PCA Plot
}
\examples{
geno <- file.path(system.file("extdata", "06_mvp-impute", package = "rMVP"), "mvp.imp")
MVP.Data.PC(TRUE, mvp_prefix=geno, out="myPC")
pca <- attach.big.matrix("myPC.pc.desc")[, 1:3]
MVP.PCAplot(PCA=pca, Ncluster=3, class=NULL, col=c("red", "green", "yellow"), file="jpg", pch=19)
}
