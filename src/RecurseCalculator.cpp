// [[Rcpp::plugins(cpp11)]]

#include <Rcpp.h>

using namespace Rcpp;
using std::pow;
using std::exp;
using std::sqrt;
using std::log;
using std::map;

// Use wrapper object to handle positions consisting of x and y coordinate.
struct Point
{
	double x;
	double y;
};

const Datetime NA_DATETIME(1.0 / 0.0); // Create NaN by dividing by 0

// Euclidean distance of points `a` and `b`.
double dist(const Point& a, const Point& b)
{
	double dX = a.x - b.x;
	double dY = a.y - b.y;
	return sqrt(dX * dX + dY * dY); // ignore Rstudio that call to sqrt is ambiguous
}

// Checks whether `a` is within `radius` of `b`.
bool isInside(const Point& a, const Point& b, double radius)
{
	return dist(a, b) <= radius;
}

// [[Rcpp::export]]
double calculateCrossingPercentage(double Cx, double Cy, 
                                   double Ax, double Ay, 
                                   double Bx, double By, 
                                   double R)
{
//	BEGIN_RCPP
	
	// algorithm based on 
	// http://stackoverflow.com/questions/1073336/circle-line-segment-collision-detection-algorithm
	// note that the origianl code description talk about 0 <= t <= 1, but though the
	// Dx and Dy vectors are directionless, the line equations go from A at t = 0 to B at t = LAB
	
	//crossing percentage
	double p = 0;
	
	// compute the euclidean distance between A and B (LAB = length AB)
	double LAB = sqrt( pow(Bx-Ax, 2) + pow(By-Ay, 2) );
		
	// compute the direction vector D from A to B
	double Dx = (Bx-Ax) / LAB;
	double Dy = (By-Ay) / LAB;
			
	// Now the line equation is x = Dx*t + Ax, y = Dy*t + Ay with 0 <= t <= LAB.
			
	// compute the value t of the closest point to the circle center (Cx, Cy)
	double t = Dx*(Cx-Ax) + Dy*(Cy-Ay);
				
	// This is the projection of C on the line from A to B.
				
	// compute the coordinates of the point E on line and closest to C
	double Ex = t*Dx+Ax;
	double Ey = t*Dy+Ay;
					
	// compute the euclidean distance from E to C
	double LEC = sqrt( pow(Ex-Cx, 2) + pow(Ey-Cy, 2) );
						
	// test if the line intersects the circle
	if( LEC < R )
	{
		// compute distance from t to circle intersection point
		double dt = sqrt( pow(R, 2) - pow(LEC, 2) );
							
		// compute first intersection point
		//Fx = (t-dt)*Dx + Ax
		//Fy = (t-dt)*Dy + Ay
									
		// compute second intersection point
		//double Gx = (t+dt)*Dx + Ax;
		//double Gy = (t+dt)*Dy + Ay;
								
		p = (t + dt) / LAB;
	}
						
	// else test if the line is tangent to circle
	else if( LEC == R )
	{
		// tangent point to circle is E
		if (Ex == Ax) // point E is A
		{
			p = 0;
		} else // point E is B
		{
			p = 1;
		}
	}
	else
	{
		// TODO: error
//		throw exception("line doesn't touch circle");
	}
	
//	return(list(LAB = LAB, t = t, p = p, Gx = Gx, Gy = Gy))
	return p;
	
//	END_RCPP
}

double calculateCrossingPercentage(const Point& C,
                                   const Point& A,
                                   const Point& B,
                                   double R)
{
	return calculateCrossingPercentage(C.x, C.y, A.x, A.y, B.x, B.y, R);
}

// [[Rcpp::export]]
IntegerVector getIsNewTrack(StringVector trajId)
{
	// first assign numeric value to each unique ID
	
	map<String, int> idMapping;
	IntegerVector numId(trajId.size());
	int nextId = 1;
	
	for (int i = 0; i < trajId.size(); i++)
	{
		if (idMapping.find(trajId[i]) != idMapping.end())
		{
			numId[i] = idMapping[trajId[i]];
		} else
		{
			numId[i] = nextId;
			idMapping[trajId[i]] = nextId;
			nextId++;
		}
	}
	
	IntegerVector isNewTrack = diff(numId); // 1 = TRUE, 0 = FALSE
	isNewTrack.push_front(1); // first track is new
	
	return isNewTrack;
}

// [[Rcpp::export]]
List getRecursionsCpp(NumericVector trajX, NumericVector trajY, 
                      DatetimeVector trajT, StringVector trajId, 
                      NumericVector locX, NumericVector locY, 
                      double radius, double threshold, String timeunits, bool verbose) 
{
	// we assume caller has checked that vectors are of same length
	double 	nTraj = trajX.size(); // number of trajectory locations
	double nLoc = locX.size(); // number of locations to examine for revisits
	
	//c("hours", "secs", "mins", "days")
	double conversionToSecs = 1.0;
	if (timeunits == "mins") { conversionToSecs = 60; } 
	else if (timeunits == "hours") { conversionToSecs =  60 * 60; } 
	else if (timeunits == "days") { conversionToSecs =  60 * 60 * 24; }
	
	threshold *= conversionToSecs;
	
	IntegerVector isNewTrack = getIsNewTrack(trajId); 
	
	// store results
	IntegerVector revisits(nLoc);
	NumericVector rt(nLoc);

	// variables for tracking each recursion
	bool stillInside = FALSE;
	Datetime radiusExitTime;
	Datetime radiusEntranceTime;
	bool appendToPreviousRevisit = FALSE;
	double timeSinceLastVisit;
	
	// vectors to store verbose results
	// entry for every recursion so we need to grow them if necessary
	int currStatSize = 10 * nLoc;
	std::vector<std::string> statsId(currStatSize);
	std::vector<double> statsX(currStatSize); 
	std::vector<double> statsY(currStatSize); 
	std::vector<int> statsCoordIdx(currStatSize); 
	std::vector<int> statsVisitIdx(currStatSize); 
	std::vector<Datetime> statsEntranceTime(currStatSize); 
	std::vector<Datetime> statsExitTime(currStatSize); 
    std::vector<double> statsTimeInside(currStatSize); 
    std::vector<double> statsTimeSinceLastVisit(currStatSize);
    int statsIdx = -1; // will get incremented before first write
    
	// Used to check the start of every location.
	const Point firstTraj {trajX[0], trajY[0]};
	
	// for each location, calculate
	for (int i = 0; i < nLoc; i++)
	{
		// stop in R
		checkUserInterrupt();
		
		double residenceTime = 0;
		
		// The current location to check against.
		Point currentLoc {locX[i], locY[i]};
		
		// reset variables for new location
		stillInside = isInside(firstTraj, currentLoc, radius); // start with animal inside radius?
		appendToPreviousRevisit = FALSE;
		radiusEntranceTime = (stillInside) ? (Rcpp::Datetime)trajT[0] : NA_DATETIME;
		radiusExitTime = NA_DATETIME;
		timeSinceLastVisit = NA_REAL;
			
		for (int j = 0; j < nTraj; j++) 
		{
			// Retrieve current trajectory position.
			Point currentTraj {trajX[j], trajY[j]};

			// Whether the current trajectory point is inside the location's radius.
			bool nowInside = isInside(currentTraj, currentLoc, radius);
			
			if (isNewTrack[j])
			{
				if (j != 0)
				{
					// need to report final revisit from previous track
					if (stillInside)
					{
						// last segment j-1 is in radius
						radiusExitTime = trajT[j-1];
						double timeInside = radiusExitTime - radiusEntranceTime;
						residenceTime += timeInside;
						
						if (appendToPreviousRevisit)
						{
							// update time inside with brief excursion time
							// use statsIdx as last written to overwrite 
							statsTimeInside[statsIdx] += timeInside;
							statsExitTime[statsIdx] = radiusExitTime;
						}
						else
						{
							revisits[i]++;
							
							if (verbose)
							{
								statsIdx++; // write new row
								
								if (statsIdx == currStatSize)
								{
									// grow output vectors
									currStatSize += 10 * nLoc;
									statsId.resize(currStatSize);
									statsX.resize(currStatSize);
									statsY.resize(currStatSize);
									statsCoordIdx.resize(currStatSize);
									statsVisitIdx.resize(currStatSize);
									statsEntranceTime.resize(currStatSize);
									statsExitTime.resize(currStatSize);
									statsTimeInside.resize(currStatSize);
									statsTimeSinceLastVisit.resize(currStatSize);
								}
								
								statsId[statsIdx] = trajId[j-1];
								statsX[statsIdx] = currentLoc.x;
								statsY[statsIdx] = currentLoc.y;
								statsCoordIdx[statsIdx] = i + 1; // because R vectors are 1-based
								statsVisitIdx[statsIdx] = revisits[i];
								statsEntranceTime[statsIdx] = radiusEntranceTime;
								statsExitTime[statsIdx] = radiusExitTime;
								statsTimeInside[statsIdx] = timeInside;
								statsTimeSinceLastVisit[statsIdx] = timeSinceLastVisit;
							}
						}
					}
				} // end if j = 0
					
				// reset variables for new trajectory
				stillInside = nowInside; // start with animal inside radius?
				appendToPreviousRevisit = FALSE;
				radiusEntranceTime = (stillInside) ? (Rcpp::Datetime)trajT[j] : NA_DATETIME;
				radiusExitTime = NA_DATETIME;
				timeSinceLastVisit = NA_REAL;
			} // end if new track
			else
			{
				// The previous trajectory position for crossing calculation.
				Point previousTraj {trajX[j-1], trajY[j-1]};

				if (!nowInside) // is location outside radius?
				{
					if (stillInside) 
					{
						// animal just moved outside
						stillInside = FALSE;
						double percentIn = calculateCrossingPercentage(currentLoc,
						                                               previousTraj,
						                                               currentTraj, radius);
						radiusExitTime = trajT[j-1] + percentIn * (trajT[j] - trajT[j-1]);
						double timeInside = radiusExitTime - radiusEntranceTime;
						residenceTime += timeInside;
						
						if (appendToPreviousRevisit)
						{
							// update exit time and time inside with current 'visit'
							statsTimeInside[statsIdx] += timeInside;
							statsExitTime[statsIdx] = radiusExitTime;
						}
						else
						{
							revisits[i]++;
							
							if (verbose)
							{
								statsIdx++; // write new row
								
								if (statsIdx == currStatSize)
								{
									// grow output vectors
									currStatSize += 10 * nLoc;
									statsId.resize(currStatSize);
									statsX.resize(currStatSize);
									statsY.resize(currStatSize);
									statsCoordIdx.resize(currStatSize);
									statsVisitIdx.resize(currStatSize);
									statsEntranceTime.resize(currStatSize);
									statsExitTime.resize(currStatSize);
									statsTimeInside.resize(currStatSize);
									statsTimeSinceLastVisit.resize(currStatSize);
								}
								
								statsId[statsIdx] = trajId[j];
								statsX[statsIdx] = currentLoc.x;
								statsY[statsIdx] = currentLoc.y;
								statsCoordIdx[statsIdx] = i + 1; // because R vectors are 1-based
								statsVisitIdx[statsIdx] = revisits[i];
								statsEntranceTime[statsIdx] = radiusEntranceTime;
								statsExitTime[statsIdx] = radiusExitTime;
								statsTimeInside[statsIdx] = timeInside;
								statsTimeSinceLastVisit[statsIdx] = timeSinceLastVisit;
							}
						}
					}
				}
				else // location inside circle
				{
					if (!stillInside) 
					{
						// animal just moved inside
						stillInside = TRUE;
						double percentIn = calculateCrossingPercentage(currentLoc,
						                                               currentTraj,
						                                               previousTraj, radius);
						radiusEntranceTime = (Rcpp::Datetime)trajT[j] - (Rcpp::Datetime)(percentIn * (trajT[j] - trajT[j-1]));
						timeSinceLastVisit = radiusEntranceTime - radiusExitTime;

						// use threshold to ignore brief trips outside
						appendToPreviousRevisit = (timeSinceLastVisit != NA_REAL) && (timeSinceLastVisit < threshold) 
							? TRUE : FALSE;
						
						if (appendToPreviousRevisit)
						{
							// update time inside with brief excursion time
							statsTimeInside[statsIdx] += timeSinceLastVisit;
							residenceTime += timeSinceLastVisit;
						}
					}
				}	
			}
		} // j loop	, trajectory locations for location i
		
		// report final revisit if any
		if (stillInside)
		{
			// last segment is in radius
			radiusExitTime = trajT[nTraj - 1];
			double timeInside = radiusExitTime - radiusEntranceTime;
			residenceTime += timeInside;
			
			if (appendToPreviousRevisit)
			{
				// update time inside with brief excursion time
				statsTimeInside[statsIdx] += timeInside;
				statsExitTime[statsIdx] = radiusExitTime;
			}
			else
			{
				revisits[i]++;
				
				if (verbose)
				{
					statsIdx++; // write new row
					
					if (statsIdx == currStatSize)
					{
						// grow output vectors
						currStatSize += 10 * nLoc;
						statsId.resize(currStatSize);
						statsX.resize(currStatSize);
						statsY.resize(currStatSize);
						statsCoordIdx.resize(currStatSize);
						statsVisitIdx.resize(currStatSize);
						statsEntranceTime.resize(currStatSize);
						statsExitTime.resize(currStatSize);
						statsTimeInside.resize(currStatSize);
						statsTimeSinceLastVisit.resize(currStatSize);
					}
					
					statsId[statsIdx] = trajId[nTraj - 1]; // last one
					statsX[statsIdx] = currentLoc.x;
					statsY[statsIdx] = currentLoc.y;
					statsCoordIdx[statsIdx] = i + 1; // becase R vectors are 1-based
					statsVisitIdx[statsIdx] = revisits[i];
					statsEntranceTime[statsIdx] = radiusEntranceTime;
					statsExitTime[statsIdx] = radiusExitTime;
					statsTimeInside[statsIdx] = timeInside;
					statsTimeSinceLastVisit[statsIdx] = timeSinceLastVisit;
				}
			}
		}
		
		rt[i] = residenceTime;
		
	} // i loop, locations to examine
	
	List results;
	
	// convert from seconds to requested units (invert conversion factor)
	transform(rt.begin(), rt.end(), rt.begin(), 
           std::bind(std::multiplies<double>(), std::placeholders::_1, 1.0 / conversionToSecs) ); // std::placeholders::_1 to replave bind1st with bind
	
	if (verbose)
	{
		transform(statsTimeInside.begin(), statsTimeInside.end(), statsTimeInside.begin(), 
            std::bind(std::multiplies<double>(), std::placeholders::_1, 1.0 / conversionToSecs) );
		transform(statsTimeSinceLastVisit.begin(), statsTimeSinceLastVisit.end(), statsTimeSinceLastVisit.begin(), 
            std::bind(std::multiplies<double>(), std::placeholders::_1, 1.0 / conversionToSecs) );
		
		
		// increment statsIdx past last value, i.e. equivalent to end()
		statsIdx++;
		DataFrame stats = DataFrame::create(_["id"] = wrap( statsId.begin(), statsId.begin() + statsIdx ),
                                      _["x"] = wrap( statsX.begin(), statsX.begin() + statsIdx ),
                                      _["y"] = wrap( statsY.begin(), statsY.begin() + statsIdx ),
                                      _["coordIdx"] = wrap( statsCoordIdx.begin(), statsCoordIdx.begin() + statsIdx ),
                                      _["visitIdx"] = wrap( statsVisitIdx.begin(), statsVisitIdx.begin() + statsIdx ),
                                      _["entranceTime"] = wrap( statsEntranceTime.begin(), statsEntranceTime.begin() + statsIdx ),
                                      _["exitTime"] = wrap( statsExitTime.begin(), statsExitTime.begin() + statsIdx ),
                                      _["timeInside"] = wrap( statsTimeInside.begin(), statsTimeInside.begin() + statsIdx ),
                                      _["timeSinceLastVisit"] = wrap( statsTimeSinceLastVisit.begin(), statsTimeSinceLastVisit.begin() + statsIdx ));

		results = List::create( _["revisits"] = revisits, _["residenceTime"] = rt,
                          _["radius"] = radius, _["revisitStats"] = stats ) ;
	}
	else
	{
		results = List::create( _["revisits"] = revisits, _["residenceTime"] = rt,
                          _["radius"] = radius) ;
	}

    return results;
}

