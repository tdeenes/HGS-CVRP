#include "Params.h"

Params::Params(std::string pathToInstance, int nbVeh, int seedRNG) : nbVehicles(nbVeh)
{
	std::string content, content2, content3;
	double serviceTimeData = 0.;
	nbClients = 0;
	totalDemandBox = 0.;
  totalDemandWt = 0.;
	maxDemandBox = 0.;
  maxDemandWt = 0.;
	durationLimit = 1.e30;
	vehicleCapacityBox = 1.e30;
  vehicleCapacityWt = 1.e30;
	isRoundingInteger = true;
	isDurationConstraint = true;

	// Initialize RNG
	srand(seedRNG);					

	// Read INPUT dataset
	std::ifstream inputFile(pathToInstance);
	if (inputFile.is_open())
	{
		getline(inputFile, content);
		getline(inputFile, content);
		getline(inputFile, content);
    std::cout << "- Process header\n";
		for (inputFile >> content ; content != "NODE_SECTION" ; inputFile >> content)
		{
			if (content == "DIMENSION") { inputFile >> content2 >> nbClients; nbClients--; } // Need to substract the depot from the number of nodes
			else if (content == "CAPACITY")	inputFile >> content2 >> vehicleCapacityBox >> vehicleCapacityWt;
			else if (content == "DURATION") { inputFile >> content2 >> durationLimit; isDurationConstraint = true; }
			else if (content == "SERVICE_TIME")	inputFile >> content2 >> serviceTimeData;
			else throw std::string("Unexpected data in input file: " + content);
		}
		if (nbClients <= 0) throw std::string("Number of nodes is undefined");
		if (vehicleCapacityBox == 1.e30) throw std::string("Vehicle capacity (box) is undefined");
    if (vehicleCapacityWt == 1.e30) throw std::string("Vehicle capacity (weight) is undefined");
		std::cout << "-- Nr of clients (excluding depot): " << nbClients << "\n";

		// Reading client data
    std::cout << "- Process node section\n";
		cli = std::vector<Client>(nbClients + 1);
		for (int i = 0; i <= nbClients; i++)
		{
			inputFile >> cli[i].custNum >> cli[i].coordX >> cli[i].coordY >> cli[i].demandBox >> cli[i].demandWt >> cli[i].serviceDuration;
			cli[i].custNum--;
			cli[i].polarAngle = CircleSector::positive_mod(32768.*atan2(cli[i].coordY - cli[0].coordY, cli[i].coordX - cli[0].coordX) / PI);
      if (cli[i].demandBox > maxDemandBox) maxDemandBox = cli[i].demandBox;
      if (cli[i].demandWt > maxDemandWt) maxDemandWt = cli[i].demandWt;
      totalDemandBox += cli[i].demandBox;
      totalDemandWt += cli[i].demandWt;
		}

    // Reading travel times
		inputFile >> content;
    std::cout << "- Process travel times section\n";
		if (content != "TRAVEL_TIME_SECTION") throw std::string("Unexpected data in input file: " + content);
    maxDist = 0.;
    timeCost = std::vector < std::vector< double > >(nbClients + 1, std::vector <double>(nbClients + 1));
    std::int32_t i_idx, j_idx;
    std::double_t ij_time;
    for (int i = 0; i < ((nbClients+1)*(nbClients+1)); i++)
    {
      inputFile >> i_idx >> j_idx >> ij_time;
      if (ij_time > maxDist) maxDist = ij_time;
      timeCost[i_idx-1][j_idx-1] = ij_time;
    }
    
		// Reading depot information (in all current instances the depot is represented as node 1, the program will return an error otherwise)
		inputFile >> content >> content2 >> content3 >> content3;
    std::cout << "- Process depot section\n";
		if (content != "DEPOT_SECTION") throw std::string("Unexpected data in input file: " + content);
		if (content2 != "1") throw std::string("Expected depot index 1 instead of " + content2);
		if (content3 != "EOF") throw std::string("Unexpected data in input file: " + content3);
	}
	else
		throw std::invalid_argument("Impossible to open instance file: " + pathToInstance);		
	
	// Default initialization if the number of vehicles has not been provided by the user
	if (nbVehicles == INT_MAX)
	{
		nbVehicles = std::ceil(1.2*totalDemandBox/vehicleCapacityBox) + 2;  // Safety margin: 20% + 2 more vehicles than the trivial bin packing LB
		std::cout << "----- FLEET SIZE WAS NOT SPECIFIED. DEFAULT INITIALIZATION TO: " << nbVehicles << std::endl;
	}

	// Calculation of the correlated vertices for each customer (for the granular restriction)
  std::cout << "- Calculate correlated vertices\n";
	correlatedVertices = std::vector < std::vector < int > >(nbClients + 1);
	std::vector < std::set < int > > setCorrelatedVertices = std::vector < std::set <int> >(nbClients + 1);
	std::vector < std::pair <double, int> > orderProximity;
	for (int i = 1; i <= nbClients; i++)
	{
		orderProximity.clear();
		for (int j = 1; j <= nbClients; j++)
			if (i != j) orderProximity.push_back(std::pair <double, int>(timeCost[i][j], j));
		std::sort(orderProximity.begin(), orderProximity.end());

		for (int j = 0; j < std::min<int>(nbGranular, nbClients - 1); j++)
		{
			// If i is correlated with j, then j should be correlated with i
			setCorrelatedVertices[i].insert(orderProximity[j].second);
			setCorrelatedVertices[orderProximity[j].second].insert(i);
		}
	}

	// Filling the vector of correlated vertices
	for (int i = 1; i <= nbClients; i++)
		for (int x : setCorrelatedVertices[i])
			correlatedVertices[i].push_back(x);

	// Safeguards to avoid possible numerical instability in case of instances containing arbitrarily small or large numerical values
	if (maxDist < 0.1 || maxDist > 100000)   throw std::string("ERROR: The distances are of very small or large scale. This could impact numerical stability. Please rescale the dataset and run again.");
	if (maxDemandBox < 0.1 || maxDemandBox > 100000 || maxDemandWt < 0.1 || maxDemandWt > 100000) throw std::string("ERROR: The demand quantities are of very small or large scale. This could impact numerical stability. Please rescale the dataset and run again.");

	// A reasonable scale for the initial values of the penalties
	penaltyDuration = 1;
	penaltyCapacityBox = std::max<double>(0.1, std::min<double>(1000., maxDist / maxDemandBox));
  penaltyCapacityWt = std::max<double>(0.1, std::min<double>(1000., maxDist / maxDemandWt));
}