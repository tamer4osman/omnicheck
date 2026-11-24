using System.Linq;
using MyApp.Store;
using static MyApp.Store.Store;
using Inv = MyApp.Store.Inventory;
using Sub = MyApp.Store.Deep;

namespace MyApp.App
{
    public class Program
    {
        public static int Main(string[] args)
        {
            var store = new Store();
            return Store.Total(args != null ? args.Length : 0);
        }
    }
}
