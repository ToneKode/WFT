#ifndef USER_H
#define USER_H

#include <string>
#include <vector>
#include <iostream>
#include "types.h"
#include "wfm_client.h"

class User
{
private:
    std::vector<Item> inventory;
    WfmClient &wftClient;
    std::string username;
    std::string id;

public:
    User(WfmClient &client) : wftClient(client) {}
    WfmClient &getClient() { return wftClient; }
    void setName(std::string name) { username = name; }

    void setID(std::string ID) { id = ID; }
    /*
    void populateInventory()
    {
        std::string input;

        while (true)
        {
            std::cout << "\nEnter item slug (or 'end' to finish): ";
            std::getline(std::cin, input);

            if (input == "end" || input == "END")
            {
                std::cout << "Finished populating inventory.\n";
                std::cout << "\nFinal inventory size: " << inventory.size() << " items\n";
                break;
            }

            try
            {
                std::cout << "Querying statistics for: " << input << "\n";
                Item item = wftClient.getItemStats(input);

                if (item.avgVolume48h > 100 && item.avgMinPrice48h > 10)
                {
                    inventory.push_back(item);
                    std::cout << " Item added to inventory - Volume: " << item.avgVolume48h
                              << ", Price: " << item.avgMinPrice48h << "\n\n";
                }
                else
                {
                    std::cout << " Item rejected - Volume: " << item.avgVolume48h
                              << " (need >100), Price: " << item.avgMinPrice48h
                              << " (need >10)\n\n";
                }
            }
            catch (const std::exception &e)
            {
                std::cout << "Error querying item: " << e.what() << "\n\n";
            }
        }
    }
*/
    };

#endif // USER_H
