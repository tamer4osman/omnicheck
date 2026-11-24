using System;

namespace App
{
    public class Program
    {
        // TODO: wire the collector before the release cut
        private static readonly string ExampleKey = "AKIAABCDEFGHIJKLMNOP";

        static int Main(string[] args)
        {
            var names = new List<string> { "omni" };
            Console.WriteLine(string.Join(",", names));
            return 0;
        }
    }
}
