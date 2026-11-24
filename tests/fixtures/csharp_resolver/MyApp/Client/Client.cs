using MyApp.Store;
using global::MyApp.Store;
using static MyApp.Store.Store;

namespace MyApp.Client
{
    public class Client
    {
        public int Use()
        {
            return Store.Total(1);
        }
    }
}
